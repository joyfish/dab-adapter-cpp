/**
 Copyright 2023 Amazon.com, Inc. or its affiliates.
 Copyright 2023 Netflix Inc.
 Copyright 2023 Google LLC
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at
 http://www.apache.org/licenses/LICENSE-2.0
 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

#pragma once

#include <thread>
#include <cstdint>
#include <initializer_list>
#include <cassert>
#include <chrono>
#include <cstring>
#include <functional>
#include <string>
#include <memory>
#include <utility>
#include <atomic>


#include "Json.h"

namespace DAB
{
    // standard exception structure to return our error code and appropriate user readable text
    struct dabException : std::exception
    {
        int64_t errorCode;
        std::string errorText;
    public:
        dabException ( int64_t errorCode, std::string errorText ) : errorCode ( errorCode ), errorText (std::move( errorText ))
        {
        }
    };

    class dabInterface;

    // our dispatcher base class.  This serves as the polymorphic interface to allow us to dispatch against specialized instances
    template< typename T >
    struct dispatcher
    {
        virtual ~dispatcher () = default;

        virtual jsonElement operator() ( T *cls, jsonElement const &elem ) = 0;
    };

    // this is the template for our dispatcher.  It itself is never instantiated, but allows us to specialize the actual templates we need
    template< size_t, size_t, class T, class F >
    struct nativeDispatch : public dispatcher<T>
    {
        nativeDispatch ()
        {
            assert ( false );
        }

        nativeDispatch ( F, std::vector<std::string> const &, std::vector<std::string> const & )
        {}

        ~nativeDispatch () = default;

        jsonElement operator() ( T *, jsonElement const & ) override
        {
            throw dabException{500, "server error"};
        }
    };

    // this is our actual dispatcher.
    // Its purpose is to take call a c++ method, but call it with parameters that are extracted from the json parameter being passed in.
    // there are two types of parameter arrays.  fixedParams whose value MUST be present in the json, and optionalParams whose value need not be present in the json, and if not there a default constructed version is passed in
    // template takes the number of fixed and optional parameters, the type of class used to dispatch against and the R ( C:: * )(Args...)  prototype for the method to call
    template< size_t nFixed, size_t nOptional, typename T, class R, class C, class ... Args >
    struct nativeDispatch<nFixed, nOptional, T, R ( C::* ) ( Args... )> : public dispatcher<T>
    {
        nativeDispatch ()
        {
            // should never be called
            assert ( false );
        }

        // the constructor takes the function pointer of the method to call, and a vector of fixed and a vector of optional parameters
        nativeDispatch ( R ( C::*func ) ( Args... ), std::vector<std::string_view> const &fixedParams, std::vector<std::string_view> const &optionalParams ) : fixedParams ( fixedParams ), optionalParams ( optionalParams )
        {
            funcPtr = func;
        }

        virtual ~nativeDispatch () = default;

        // this is the main dispatch entry point.  It takes a pointer to the class of the method to call, and the jsonElement containing any fixed and/or optional parameters to extract and call the method with
        jsonElement operator() ( T *cls, jsonElement const &elem ) override
        {
            // call the fixed position of our dispatcher.   This is
            return callFixed<0, 0> ( cls, elem, types < Args... > {} );
        }

    private:

        // this is the actual function we wish to dispatch against
        R ( C::*funcPtr ) ( Args... );

        // these are the names of the fixed parameters we
        std::vector<std::string_view> fixedParams;
        std::vector<std::string_view> optionalParams;

        // type-list for our meta-program below   This struct is blank and only servers to specialize functions based on the type parameter pack being passed in.
        template< class ... >
        struct types
        {
        };

        // start iterating through any fixed parameters.   We look up the element in the jsonElement class and recurse
        //     into the function again with the looked up element at the end of the parameter list
        //     this results in a function call with the jsonElements automatically discovered
        //     the first two parameters are which fixed and optional parameters to extract
        template< size_t fixed, size_t optional, class Head, class ... Tail, class ...Vs >
        jsonElement callFixed ( T *cls, jsonElement const &elem, types<Head, Tail ...>, Vs &&...vs )
        {
            if constexpr ( fixed < nFixed )
            {
                // extract the fixedParams (the one we're current extracting is passed in by the first template parameter
                // then recurse but call the next template parameter,  the extracted parameters are appended onto the end as a VS...vs parameter pack
                // we check first in "payload" and second in the base json to allow us to pass in either type of value as the parameter (for instance context)
                if ( elem["payload"].has ( fixedParams[fixed] ))
                {
                    return callFixed<fixed + 1, optional> ( cls, elem, types<Tail...>{}, std::forward<Vs> ( vs )..., elem["payload"][fixedParams[fixed]] );
                } else if ( elem.has ( fixedParams[fixed] ))
                {
                    return callFixed<fixed + 1, optional> ( cls, elem, types<Tail...>{}, std::forward<Vs> ( vs )..., elem[fixedParams[fixed]] );
                } else if ( fixedParams[fixed] == "*" )
                {
                    // you can use the * to receive the entire json object without being parsed into parameters
                    return callFixed<fixed + 1, optional> ( cls, elem, types<Tail...>{}, std::forward<Vs> ( vs )..., elem );
                } else
                {
                    throw dabException{400, std::string ( "missing parameter \"" ) + fixedParams[fixed].data () + "\""};
                }
            } else
            {
                // we have extracted all the fixed parameters, no call start extracting any optional parameters
                return callOptional<fixed, optional> ( cls, elem, types<Head, Tail...>{}, std::forward<Vs> ( vs )... );
            }
        }

        // for cases with NO optional parameters - type list has been exhausted and there are no optional parameters
        template< size_t fixed, size_t optional, class ...Vs >
        jsonElement callFixed ( T *cls, jsonElement const &, types<>, Vs &&...vs )
        {
            static_assert ( fixed == nFixed );
            static_assert ( !optional );

            if constexpr ( std::is_same_v<R, void> )
            {
                (cls->*funcPtr) ( std::forward<Vs> ( vs )... );
                return {};
            } else
            {
                return (cls->*funcPtr) ( std::forward<Vs> ( vs )... );
            }
        }

        // start extracting the optional parameters and looking them up in the jsonElement.
        //     If the desired element isn't present in the passed in json object, then we just
        //     create a default-initialized value of type HEAD
        template< size_t fixed, size_t optional, class Head, class ... Tail, class ...Vs >
        jsonElement callOptional ( T *cls, jsonElement const &elem, types<Head, Tail ...>, Vs &&...vs )
        {
            // see if the desired element is present
            if ( elem["payload"].has ( optionalParams[optional] ))
            {
                // it is, so extract and call it
                return callOptional<fixed, optional + 1> ( cls, elem, types<Tail...>{}, std::forward<Vs> ( vs )..., elem["payload"][optionalParams[optional]] );
            } else if ( elem.has ( optionalParams[optional] ))
            {
                // it is, so extract and call it
                return callOptional<fixed, optional + 1> ( cls, elem, types<Tail...>{}, std::forward<Vs> ( vs )..., elem[optionalParams[optional]] );
            } else
            {
                // it's not so create a default initialized value of the desired type
                return callOptional<fixed, optional + 1> ( cls, elem, types<Tail...>{}, std::forward<Vs> ( vs )..., Head{} );
            }
        }

        // we've now generated function parameters (vs) for all our specified fixed and optional parameters, and it's now time to call the function
        template< size_t fixed, size_t optional, class ...Vs >
        jsonElement callOptional ( T *cls, jsonElement const &, types<>, Vs &&...vs )
        {
            static_assert ( fixed == nFixed );
            static_assert ( optional == nOptional );

            // test to see if the function's return type is void, if it is, than just create a jsonElement as a return type
            if constexpr ( std::is_same_v<R, void> )
            {
                (cls->*funcPtr) ( std::forward<Vs> ( vs )... );
                return {};
            } else
            {
                // already returning desired return value so just call the function
                return (cls->*funcPtr) ( std::forward<Vs> ( vs )... );
            }
        }
    };

    // this is our unspecialized dabInterface class.   It contains a minimal amount of functionality
    //     (storing the publish callback as well as the interface to call it)
    //     everything else is implemented in dabClient which inherits from this class
    class dabInterface
    {
        std::function< void(jsonElement const &) > publishCallback;

    public:
        virtual ~dabInterface () = default;

        virtual jsonElement dispatch ( jsonElement const &json ) = 0;

        // set the callback for publishing (sending out telemetry)
        void setPublishCallback ( decltype ( publishCallback) cb )
        {
            publishCallback = std::move(cb);
        }

        // actually call the publish callback with the supplied json
        virtual void publish ( jsonElement const &elem )
        {
            (publishCallback) ( elem );
        }

        // do nothing routine to return an array of topics that this class supports (for mqtt subscription)
        virtual std::vector<std::string> getTopics ()
        {
            return {};
        }
    };

    template< typename T >
    class dabClient : public dabInterface
    {
        const std::string protocolVersion = "2.0";          // version of the DAB protocol being implemented
        std::string ipAddress;                              // ip address for dab/discovery response

        // this is an XMACRO list of def() macro's.   It contains the dab method name, the name of the method to call and to arrays of fixed and optional parameters defined as string literals
        // NOTE: multiple fixed or optional parameters need to be enclosed in ()   this is a preprocessor limitation, it will work just fine if you do this
#define METHODS \
            def( "/operations/list", opList, opList, {}, {} )                                                                                       \
            def( "/applications/list", appList, appList, {}, {} )                                                                                   \
            def( "/applications/launch", appLaunch, appLaunch, {"appId"}, {"parameters"} )                                                          \
            def( "/applications/launch-with-content", appLaunchWithContent, appLaunchWithContent, ({ "appId", "contentId" }), { "parameters" } )    \
            def( "/applications/get-state", appGetState, appGetState, { "appId" }, {} )                                                             \
            def( "/applications/exit", appExit, appExit, {"appId"}, {"background"} )                                                                \
            def( "/device/info", deviceInfo, deviceInfo, {}, {} )                                                                                   \
            def( "/system/restart", systemRestart, systemRestart, {}, {} )                                                                          \
            def( "/system/settings/list", systemSettingsList, systemSettingsList, {}, {} )                                                          \
            def( "/system/settings/get", systemSettingsGet, systemSettingsGet, {}, {} )                                                             \
            def( "/system/settings/set", systemSettingsSet, systemSettingsSet, { "*" }, {} )                                                 \
            def( "/input/key/list", inputKeyList, inputKeyList, {}, {} )                                                                            \
            def( "/input/key-press", inputKeyPress, inputKeyPress, { "keyCode"}, {} )                                                               \
            def( "/input/long-key-press", inputKeyLongPress, inputKeyLongPress, ({ "keyCode", "durationMs" }), {} )                                \
            def( "/output/image", outputImage, outputImage, {}, {} )                                                                                \
            def( "/device-telemetry/start", deviceTelemetry, deviceTelemetryStartInternal, ({ "duration" }), {} )                          \
            def( "/device-telemetry/stop", deviceTelemetry, deviceTelemetryStopInternal, {}, {} )                                                   \
            def( "/app-telemetry/start", appTelemetry, appTelemetryStartInternal, ({ "appId", "duration" }), {} )                          \
            def( "/app-telemetry/stop", appTelemetry, appTelemetryStopInternal, {"appId"}, {} )                                                     \
            def( "/health-check/get", healthCheckGet, healthCheckGet, { }, {} )                                                                     \
            def( "/voice/list", voiceList, voiceList, { }, {} )                                                                                     \
            def( "/voice/set", voiceSet, voiceSet, { "voiceSystem" }, {} )                                                                         \
            def( "/voice/send-audio", voiceSendAudio, voiceSendAudio, { "fileLocation" }, {"voiceSystem" } )                                       \
            def( "/voice/send-text", voiceSendText, voiceSendText, { "requestText" }, {"voiceSystem" } )                                           \
            def( "/version", version, version, { }, {} )

        // map by operation storing a pointer to the dispatcher and a bool if it has been implemented by the user
        std::map<std::string, std::pair<std::unique_ptr<dispatcher<T>>, bool>> dispatchMap;

        // telemetry mutex and condition variable for scheduling
        std::mutex telemetryAccess;
        std::condition_variable telemetryCondition;

        // base telemetry Executor class.   This will be specialized and should never be called directly.
        // we need this as the executor is polymorphic based on passed in types
        class telemetryExecutor
        {
        public:
            virtual ~telemetryExecutor () = default;

            virtual std::chrono::time_point<std::chrono::steady_clock> getNextScheduledTime ()
            {
                return std::chrono::steady_clock::now ();
            }

            virtual jsonElement getTelemetry ()
            {
                return {};
            }

            virtual void setInterval ( std::chrono::milliseconds newInterval )
            {}
        };

        // this is the actual specialized telemetry executor
        // it stores:
        //      the callback to handle the telemetry
        //      the interval between callbacks
        template< typename F >
        class telemetry : public telemetryExecutor
        {
            std::chrono::milliseconds interval;
            F callback;
        public:
            virtual ~telemetry () = default;

            telemetry ( std::chrono::milliseconds interval, F getTelemetryCallback ) : interval ( interval ), callback ( getTelemetryCallback )
            {
            }

            std::chrono::time_point<std::chrono::steady_clock> getNextScheduledTime () override
            {
                return std::chrono::steady_clock::now () + interval;
            }

            jsonElement getTelemetry () override
            {
                return callback ();
            }
            void setInterval ( std::chrono::milliseconds newInterval ) override
            {
                interval = newInterval;
            }
        };

        // this is the structure used to schedule our telemetry callbacks.  Fundamentally it is a map with the index value being the next time for a callback
        // we use a wait_until condition variable based on the front of the map (the next time something needs to be triggered).
        // we can add things in at any time by simply doing a notify on the condition variable if we add additional telemetry slots in the future
        std::map<std::chrono::time_point<std::chrono::steady_clock>, std::tuple<std::string, std::string, std::unique_ptr<telemetryExecutor> >> telemetryScheduler;

        // callback to add data to telemetry
        template< typename F >
        void addTelemetry ( std::chrono::milliseconds interval, std::string const &id, std::string const &topic, F getTelemetryCallback )
        {
            std::lock_guard l1 ( telemetryAccess );

            // iterate through our telemetry to see if the app(or device) already exists, if so, just update the interval
            for ( auto it = telemetryScheduler.begin(); it != telemetryScheduler.end(); it++ )
            {
                if ( std::get<0>(it->second) == id )
                {
                    std::get<2>(it->second).get()->setInterval ( interval );
                    telemetryCondition.notify_all ();
                    return;
                }
            }
            // schedule for NOW so we send one immediately
            telemetryScheduler.insert ( std::move ( std::pair ( std::chrono::steady_clock::now (), std::move(std::tuple(id, topic, std::make_unique<telemetry<F>> ( interval, getTelemetryCallback ) )) )));
            telemetryCondition.notify_all ();
        }

        // pretty self-explanatory, if it exists delete it
        void deleteTelemetry ( std::string const &id )
        {
            std::lock_guard l1 ( telemetryAccess );

            for ( auto it = telemetryScheduler.begin(); it != telemetryScheduler.end(); it++ )
            {
                if ( std::get<0>(it->second) == id )
                {
                    /* already exists, so update the scheduled time */
                    telemetryScheduler.erase ( it );
                    telemetryCondition.notify_all ();
                    return;
                }
            }
        }

        // telemetryTask is a worker thread, we use the exiting boolean to allow us to exit cleanly
        std::atomic<bool> exiting = false;

        // this is our main scheduling thread
        void telemetryTask ()
        {
            // keep going so long as our exiting boolean has not been set to true
            while ( !exiting )
            {
                std::unique_lock l1 ( telemetryAccess );
                if ( telemetryScheduler.empty() )
                {
                    // nothing to schedule so just wait until our condition variable gets notified
                    telemetryCondition.wait ( l1 );
                } else
                {
                    // wait until either our condition variable gets notified (something added or deleted or exiting)
                    //    or until our next-scheduled telemetry time is exceeded
                    telemetryCondition.wait_until( l1, telemetryScheduler.begin ()->first );
                }
                if ( !telemetryScheduler.empty ())
                {
                    // check to see if our next to fire event time has been passed, if so get the telemetry data and publish it
                    if ( telemetryScheduler.begin ()->first < std::chrono::steady_clock::now ())
                    {
                        // get the telemetry data (calling the callback passed in during addTelemetry)
                        auto rsp = std::get<2>(telemetryScheduler.begin ()->second).get()->getTelemetry ();
                        // call the publish callback to send the telemetry data to any subscribers
                        publish ( { { "topic", std::get<1>(telemetryScheduler.begin ()->second) }, {"payload", rsp} } );

                        // extract the node entry, calculate a new key value (execution time) and reinsert (no reallocation or copying, just some pointer manipulation so this is fast
                        auto nodeHandle = telemetryScheduler.extract ( telemetryScheduler.begin ()->first );
                        nodeHandle.key () = std::get<2>(nodeHandle.mapped()).get()->getNextScheduledTime ();
                        telemetryScheduler.insert ( std::move(nodeHandle) );
                    }
                }
            }
        }

    protected:
        // the deviceID for this client
        std::string deviceId;

    public:

        std::thread  telemetryThreadId;

        explicit dabClient ( std::string const &deviceId, std::string const &ipAddress ) : deviceId ( deviceId ), ipAddress ( ipAddress )
        {
            // XMACRO instantiation of our list of method names, methods and fixed and optional parameters
            // this is resolved into a map of method name and a pair of unique pointers to a nativeDispatcher
            //     instance and a bool indicating if the method was overridden by the instantiating class (must be done using CRTP)
#define def( methName, detectFunc, callFunc, fixedParams, optionalParams )                                                                                                                                                                                            \
                {                                                                                                       \
                    auto disp = std::make_unique<nativeDispatch<std::initializer_list<char const *>fixedParams.size (), std::initializer_list<char const *>optionalParams.size (), T, decltype(&T::callFunc)>> ( &T::callFunc, std::vector<std::string_view> fixedParams, std::vector<std::string_view> optionalParams );   \
                    auto p1 = std::make_pair ( std::move ( disp ), !std::is_same_v<decltype(&dabClient::detectFunc), decltype(&T::detectFunc)> || !strcmp ( "/operations/list", (methName) ) || !strcmp ( "/version", (methName) ) );                                    \
                    auto p2 = std::make_pair ( std::string ( "dab/" ) + deviceId + (methName), std::move ( p1 ) );                                                                                                                                            \
                    dispatchMap.insert ( std::move ( p2) );                                                                                                                                                                                                    \
                }
            METHODS

            // dab/discovery.   special as it doesn't have deviceID
            {                                                                                                       \
                    auto disp = std::make_unique<nativeDispatch<0, 0, T, decltype(&T::discovery)>> ( &T::discovery, std::vector<std::string_view> {}, std::vector<std::string_view> {} );   \
                    auto p1 = std::make_pair ( std::move ( disp ), false );                                    \
                    auto p2 = std::make_pair ( std::string ( "dab/discovery" ), std::move ( p1 ) );                                                                                                                                            \
                    dispatchMap.insert ( std::move ( p2) );                                                                                                                                                                                                    \
            }

            telemetryThreadId = std::thread ( &dabClient::telemetryTask, this );
        }

        // this is the getTopics instantiation.  It returns a list of all the operations we support so that we subscribe to them
        // to the mqtt broker
        std::vector<std::string> getTopics () override
        {
            std::vector<std::string> topics;
            for ( auto const &it: dispatchMap )
            {
                if ( it.second.second )
                {
                    // return operation, but trim off leading dab/<deviceId>/
                   topics.push_back ( it.first );
                }
            }
            return topics;
        }

        ~dabClient () override
        {
            // set exiting, notify our telemetry worker thread and wait for it to exit
            exiting = true;
            telemetryCondition.notify_all();
            telemetryThreadId.join();
        }

        // this is our implementation of opList.   It uses the overridden bool to specify if the operation is supported and only returns operations that the client supports
        jsonElement opList ()
        {
            jsonElement elem;
            for ( auto const &it: dispatchMap )
            {
                if ( it.second.second )
                {
                    // return operation, but trim off leading dab/<deviceId>/
                    elem["operations"].push_back ( std::string ( it.first.c_str() + it.first.find ( '/', it.first.find ( '/' ) + 1 ) + 1 ) );
                }
            }
            return elem;
        }

        // returns the currently supported protocol version
        jsonElement version ()
        {
            jsonElement elem;
            elem["versions"].push_back ( protocolVersion );
            return elem;
        }

        // returns the currently supported protocol version
        jsonElement discovery ()
        {
            return {{"ip", ipAddress}, {"deviceId", deviceId} };
        }
        // this is the internal implementation for deviceTelemetryStart.  This is NOT the override for the users telemetry call
        //    this function takes the duration and sets up the calls to the appropriate telemetry method.  That method id described
        //    lower down in the codebase
        jsonElement deviceTelemetryStartInternal ( int64_t durationMs )
        {
            if constexpr ( std::is_member_function_pointer_v<decltype ( &T::deviceTelemetry )> )
            {
                // construct the topic to publish on and add the telemetry with the lambda that calls the deviceTelemetry() method (which is what the user needs to implement)
                addTelemetry ( std::chrono::milliseconds ( durationMs ), "", std::string ( "dab/" ) + deviceId + "/device-telemetry/metrics" , [this] () { return (static_cast<T*>(this)->*(&T::deviceTelemetry )) (  ); } );
                return {{"duration", durationMs}};
            } else
            {
                throw dabException ( 400, "device telemetry not supported" );
            }
        }

        // this is the device telemetry stop handler.  The user need not worry about this, once this is called they will simply no longer receive device telemetry callbacks
        jsonElement deviceTelemetryStopInternal ()
        {
            deleteTelemetry ( "" );
            return {};
        }

        // this is the internal implementation for applicationTelemetryStart.  This is NOT the override for the users telemetry call
        //    this function takes the duration and sets up the calls to the appropriate telemetry method.  That method id described
        //    lower down in the codebase
        jsonElement appTelemetryStartInternal ( std::string const &appId, int64_t durationMs )
        {
            if constexpr ( std::is_member_function_pointer_v<decltype ( &T::appTelemetry )> )
            {
                // construct the topic to publish on and add the telemetry with the lambda that calls the appTelemetry() method (which is what the user needs to implement)
                addTelemetry ( std::chrono::milliseconds ( durationMs ), appId, std::string ( "dab/" ) + deviceId + "/app-telemetry/metrics/" + appId , [this, appId] () { return (static_cast<T*>(this)->*(&T::appTelemetry )) ( appId ); } );
                return {{"duration", durationMs}};
            } else
            {
                throw dabException ( 400, "app telemetry not supported" );
            }
        }

        // this is the device telemetry stop handler.  The user need not worry about this, once this is called they will simply no longer receive application telemetry callbacks
        jsonElement appTelemetryStopInternal ( std::string const &appId )
        {
            deleteTelemetry ( appId );
            return {};
        }

        // our main dispatch entry point.
        // this function takes in the json, extracts the topic, response topic, any correlation data
        // it then calls the proper user handler, takes the payload response, builds the full response and
        // publishes it using the response topic.
        // it catches any exceptions and builds appropriate dab error responses should a failure occur
        jsonElement dispatch ( jsonElement const &elem ) override
        {
            jsonElement rsp;
            try
            {
                std::string topic = elem["topic"];

                auto it = dispatchMap.find ( topic );
                if ( it != dispatchMap.end ())
                {
                    rsp = (*it->second.first) ( static_cast<T *>(this), elem );
                }
                if ( !rsp.has ( "status" ))
                {
                    rsp["status"] = 200;
                }
            } catch ( std::pair<int, std::string> &e )
            {
                rsp = { { "status", e.first, "error", e.second } };
            } catch ( std::pair<int, char const *> &e )
            {
                rsp = { { "status", e.first, "error", e.second } };
            } catch ( dabException &e )
            {
                rsp = { { "status", e.errorCode, "error", e.errorText } };
            } catch ( ... )
            {
                rsp = { { "status", 400, "error", "unable to parse request" } };
            }
            return rsp;
        }

        /* support function to execute a system command and return the results */
        std::string execCmd ( std::string const &cmd )
        {
            try
            {
                int retCode = 0;
                std::string output;

                auto close = [&retCode] ( FILE *file ) {
#ifdef _WIN32
                    retCode = _pclose ( file );
#else
                    retCode = pclose ( file );
#endif
                };

#ifdef _WIN32
                std::unique_ptr<FILE, decltype(close)> pipe ( _popen ( cmd.c_str (), "r" ), close );
#else
                std::unique_ptr<FILE, decltype ( close )> pipe ( popen ( cmd.c_str (), "r" ), close );
#endif
                if ( !pipe )
                {
                    throw std::runtime_error ( "popen() failed!" );
                }

                char tmpBuff[4096];
                while ( fgets ( tmpBuff, sizeof (tmpBuff), pipe.get ()) != nullptr )
                {
                    output += tmpBuff;
                }
                return output;
            } catch ( ... )
            {
                throw dabException{500, std::string ( "executing command \"" ) + cmd + "\" returned error " + std::to_string (errno)};
            }
        }

        /*
            DAB METHODS

            These functions are place-holder/prototypes used in the meta-template parameter deduction above.
            The opList will detect if these functions have been overridden by the users DAB class and only emit operations that have been overridden
        */

        jsonElement appList ()
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement appLaunch ( std::string const &appId, jsonElement const &elem )
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement appLaunchWithContent ( std::string const &appId, std::string const &contentId, jsonElement const &elem )
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement appGetState ( std::string const &appId )
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement appExit ( std::string const &appId, bool background )
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement deviceInfo ()
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement systemRestart ()
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement systemSettingsList ()
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement systemSettingsGet ()
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement systemSettingsSet ( jsonElement const &elem )
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement inputKeyList ()
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement inputKeyPress ( std::string const & )
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement inputKeyLongPress ( std::string const &keyCode, int64_t durationMs )
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement outputImage ()
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement deviceTelemetry ()
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement appTelemetry ( std::string const &appId )
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement healthCheckGet ()
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement voiceList ()
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement voiceSet ( jsonElement const &voiceSystem )
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement voiceSendAudio ( std::string const &fileLocation, std::string const &voiceSystem )
        {
            throw dabException{501, "unsupported"};
        }

        jsonElement voiceSendText ( std::string const &requestText, std::string const &voiceSystem )
        {
            throw dabException{501, "unsupported"};
        }
    };
};