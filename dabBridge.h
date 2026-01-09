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

#include <string>
#include <cstring>
#include "dabClient.h"
#include <cassert>

namespace DAB
{

    // the dabBridge template serves as the main <deviceId> switching dispatch entry point.
    // it takes a list of class types, each of which must support a static isCompatible method to determine if that class can handle the specified device
    // Instances of the handler object are created by issuing a makeInstance( <deviceId>[, <ipAddress>, [<params...>]] ) call
    // the object created is will be routed any calls destined for the specified <deviceId>
    // the <ipAddress> is optional in this call.  If it is left out, the first type on in the type list will be instantiated.  This is the "on-device" mode.   If multiple
    // on-device classes are possible, simply pass in a value to be call their isCompatible method.
    // <ipAddress> and <params...> are type agnostic, however isCompatible is defined in bridge mode to take a string containing the ipAddress of the end-device.

	// type list should be a list of types inheriting from dabClient (which itself inherits from dabInterface which is the base class we're interested in)
	template<typename ... C>
	class dabBridge {
        std::map<std::string, std::unique_ptr<dabInterface>, std::less<>> instances;

        // type list for our meta-program below
        template<class ...>
        struct types {
        };

    public:

        virtual ~dabBridge() = default;

        std::function< void(jsonElement const &) > publishCallback;

        // main topic dispatch entry point.   It extracts the topic, removes the dab/<device_id>/ portion and tries to find it in our map.  If it is there
        // it will dispatch against the stored dispatcher (which will build the parameter lists from the passed in json and then call the specified class method
        virtual jsonElement dispatch( jsonElement const &json ) {
            if (json.has("topic")) {
                std::string const &topic = json["topic"];
                const char *topic_cstr = topic.c_str();    

                if ( topic == "dab/discovery")
                {
                    // we may have multiple devices and each one needs to send a response.   However, we can only return one response.
                    // so we'll instead, iterate through the second device and call publishCallback
                    auto it = instances.begin();
                    it++;
                    for ( ; it != instances.end(); it++ )
                    {
                        auto &[name, value] = *it;
                        publishCallback ( it->second->dispatch ( json ) );
                    }
                    // return as a response our first class's response
                    return instances.begin()->second->dispatch ( json );
                } else if (starts_with(topic_cstr, "dab/"))
                {
                    // auto slashPos = std::string_view(topic.begin() + 4, topic.end()).find_first_of('/');
                    auto slashPos = std::string_view(topic.c_str() + 4, topic.size() - 4).find_first_of('/');

                    if (slashPos == std::string::npos) {
                        throw DAB::dabException ( 400, "topic is malformed" );
                    }

                    // the deviceId is extracted from "dab/<deviceId>/<method>"
                    // auto deviceId = std::string_view(topic.begin() + 4, topic.begin() + 4 + (int)slashPos);
                    std::string deviceIdStr(topic.begin() + 4, topic.begin() + 4 + static_cast<std::ptrdiff_t>(slashPos));
                    auto deviceId = std::string_view(deviceIdStr.c_str(), deviceIdStr.size());

                    auto it = instances.find(deviceId);
                    if (it != instances.end()) {
                        // now call the dabInterface associated with the deviceId;
                        return it->second->dispatch(json);
                    } else {
                        throw DAB::dabException ( 400, "deviceId does not exist" );
                    }
                } else {
                    throw DAB::dabException ( 400, "topic is malformed" );
                }
            } else {
                throw DAB::dabException ( 400, "no topic found" );
            }
        }

        // return a list of all operations supported by the specified class.   This is solely determined by implementation of the handler method.
        std::vector<std::string> getTopics() {
            std::vector<std::string> topics;

            topics.reserve(instances.size());
            for (auto const &instance: instances) {
                auto newTopics = instance.second->getTopics();
                topics.insert ( topics.end(), newTopics.begin(), newTopics.end() );
            }
            topics.push_back( "dab/discovery");
            return topics;
        }

    	bool starts_with(const char*string, const char* pattern)
    	{
    		while (*pattern && *string == *pattern)
    		{
    			string++;
    			pattern++;
    		}

    		return *pattern == 0;
    	}

        // This iterates through all the class's and sets the mqtt publish callback so that the class's can publish notifications (non-request/response)
        template<typename F>
        void setPublishCallback(F f)
        {
            for ( auto &it : instances )
            {
                it.second->setPublishCallback( f );
            }
            publishCallback = f;
        }

        // makeInstance will instantiate a dabInterface object.  It will iterate through all types and call the static member function isCompatible( <params>...)
        // if this returns true, then the corresponding class will be instantiated and associated with the passed in device<id>
        template <typename ...VS>
        dabInterface *makeDeviceInstance ( char const *deviceId, VS  &&...vs )
        {
            return makeInstances<0> ( deviceId, types<C...>{}, std::forward<VS>(vs)... );
        }
		private:

        template <typename FIRST, typename ...VS>
        FIRST &getFirstParameter ( FIRST &&first, VS &&... ) {
            return first;
        }
		// this is a recursive template that eats away one of our template type parameters at a time (HEAD).  It calls isCompatible on each of the classes (passing in any user-passed in
        // parameters until one returns true (or it subsequently fails and throws an exception).   if isCompatible() returns true, that class is instantiated and the search ends.
        //
        // alternately, if no parameters to isCompatible() have been passed, the first class in the list will always be instantiated.  This is the on-device mode.
		template<int dummy, class HEAD, class ... Tail, class ...VS>
		dabInterface *makeInstances ( char const *deviceId, types<HEAD, Tail...>, VS &&...vs )
		{
            if ( sizeof... ( VS ) ) {
                // check the name of type HEAD and see if it's the one we want to instantiate
                if ( HEAD::isCompatible ( getFirstParameter(std::forward<VS>(vs)... ) ) ) {
                    // it is, so instantiate HEAD and save a unique pointer to it in our map.  The key is the UUID
                    instances.insert(std::move(std::make_pair(std::move(std::string(deviceId)), std::move(std::make_unique<HEAD>(deviceId, std::forward<VS>(vs)...)))));
                    return instances.find(std::string_view(deviceId))->second.get();
                } else {
                    return makeInstances<dummy>(deviceId, types<Tail...>{}, std::forward<VS>(vs)...);
                }
            } else {
                instances.insert(std::move(std::make_pair(std::move(std::string(deviceId)), std::move(std::make_unique<HEAD>(deviceId, std::forward<VS>(vs)...)))));
                return instances.find(std::string_view(deviceId))->second.get();
            }
		}

		// we need dummy here otherwise, once HEAD and ...TAIL are exhausted, the template argument list is <> which becomes an invalid specialization.
        // this case is reached when all passed in classes have been exhausted and all have returned false on their respective isCompatible() calls
		template< int , typename ...VS >
		dabInterface *makeInstances ( char const *, types<>, VS &&... ) {
            // if we ever got here, then we never found the proper class name to instantiate
            throw DAB::dabException ( 400, "no compatible devices found" );
        }
	};
}
