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

#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>
#include <variant>
#include <type_traits>
#include <memory>
#include <utility>
#include <cstdlib>
#include <string>
#include <initializer_list>

namespace DAB
{
    class jsonElement
    {
    public:
        typedef std::map <std::string, jsonElement, std::less<>> objectType;
        typedef std::vector <jsonElement> arrayType;
        inline static struct
        {
        } array{};            // this is used to force an indeterminate { "a, "b" } to be processed as an array and not as an object

    private:

        std::variant<std::monostate, int64_t, double, std::string, objectType, arrayType, bool, decltype ( array )> value;

        template< typename, typename >
        struct is_associative_container
        {
            static constexpr bool value = false;
        };

        template< typename C >
        struct is_associative_container<C, typename C::mapped_type>
        {
            static constexpr bool value = true;
        };

        template< typename, typename, typename >
        struct is_sequence_container
        {
            static constexpr bool value = false;
        };

        template< typename C >
        struct is_sequence_container<C, typename C::value_type, decltype ( std::declval<C> ().clear ())>
        {
            static constexpr bool value = true;
        };

    public:
        jsonElement ()
        {}

        template< typename T >
        jsonElement ( std::string const &name, T v )
        {
            if constexpr ( std::is_same_v < bool, T > )
            {
                value = objectType{{name, (bool) v}};
            } else
            {
                value = objectType{{name, v}};
            }
        }

        // the constructor takes an initializer list
        //     if it's a fundamental value (int64_t, bool, double, std::string} it simply creates a jsonElement that holds that type
        //     if it's an object of type { "name", "value" } it interprets this as a name value pair and adds this to the surrounding object;
        //     if it's a list of more than two values, it is interpreted as an array.   Alternately if it is two values exactly, an array
        //     can be declared as { jsonElement::array(), "one", "two" }   If a jsonElement::array() is detected it is treated as a flag
        //     and does not become part of the json data
        jsonElement ( std::initializer_list <jsonElement> i )
        {
            if ( i.size () == 2 )
            {
                if ( std::holds_alternative<std::string> ( i.begin ()->value ))
                {
                    value = objectType{{std::get<std::string> ( i.begin ()->value ), std::next ( i.begin ())->value}};
                    return;
                }
            }
            bool isObject = true;
            for ( auto &it: i )
            {
                if ( !std::holds_alternative<objectType> ( it.value ))
                {
                    isObject = false;
                    break;
                }
            }

            if ( isObject )
            {
                if ( !std::holds_alternative<objectType> ( value ))
                {
                    value = objectType ();
                }
                auto &obj = std::get<objectType> ( value );
                for ( auto &it: i )
                {
                    auto &elem = std::get<objectType> ( it.value );

                    obj.insert ( elem.begin (), elem.end ());
                }
            } else
            {
                if ( !std::holds_alternative<arrayType> ( value ))
                {
                    value = arrayType ();
                }
                auto &arr = std::get<arrayType> ( value );
                for ( auto &it: i )
                {
                    // if it's of type array() ignore the value.  This is just to indicate that we process as an array and not as an object
                    if ( !std::holds_alternative<decltype ( array )> ( it.value ))
                    {
                        arr.push_back ( it );
                    }
                }
            }
        }

        // initializer for an associative container.
        template< class T, typename std::enable_if_t<is_associative_container<T, T>::value> * = nullptr >
        jsonElement ( T const &o ) : value ( objectType ( o.cbegin (), o.cend ()))
        {}

        // initializer for an array
        template< class T, typename std::enable_if_t<is_sequence_container<T, T, T>::value> * = nullptr >
        jsonElement ( T const &a ) : value ( arrayType ( a.cbegin (), a.cend ()))
        {}

        // for array... needs to have a vector type of <jsonElement>
        template< class T, typename std::enable_if_t<!is_sequence_container<T, T, T>::value && !is_associative_container<T, T>::value> * = nullptr >
        jsonElement ( T const &v )
        {
            if constexpr ( std::is_same_v<const char *, T> )
            {
                value = std::string ( v );
            } else if constexpr ( std::is_same_v < bool, T > )
            {
                value = (bool) v;
            } else if constexpr ((std::is_integral_v < T > || std::is_enum_v < T > ) && !std::is_same_v < bool, T > )
            {
                value = (int64_t) v;
            } else if constexpr ( std::is_floating_point_v < T > )
            {
                value = (double) v;
            } else
            {
                value = v;
            }
        }

        virtual ~jsonElement ()
        {}

        // this is the main parser for the jsonElement class
        // it takes the pointer to a json string and returns a jsonObject as a result
        // it is fully recursive
        jsonElement ( char const **str )
        {
            while ( isSpace ( **str ))
                (*str)++;        // skip spaces and eol characters

            // see if we're parsing an object
            if ((*str)[0] == '{' )
            {
                // we're a json object
                (*str)++;

                value = jsonElement::objectType ();

                auto &obj = std::get<jsonElement::objectType> ( value );

                bool first = true;
                for ( ;; )
                {
                    // skip any whitespace
                    while ( isSpace ( **str ))
                        (*str)++;        // skip spaces and eol characters

                    // are we one parsing the object?
                    if ((*str)[0] == '}' )
                    {
                        (*str)++;
                        break;
                    }

                    // if this isn't our first name/value pair we should have a , as the next character
                    if ( !first )
                    {
                        if ((*str)[0] != ',' )
                        {
                            throw "missing comma";
                        }
                        (*str)++;
                        while ( isSpace ( **str ))
                            (*str)++;        // skip spaces and eol characters
                        if ((*str)[0] == '}' )
                        {
                            (*str)++;
                            break;
                        }
                    }
                    first = false;

                    std::string name;
                    // are we quoted name : values?   We can handle either
                    if ((*str)[0] == '"' )
                    {
                        // quoted
                        (*str)++;
                        while ( **str && (*str)[0] != '"' )
                        {
                            name += *((*str)++);
                        }
                        if ( **str )
                        {
                            (*str)++;
                        } else
                        {
                            throw "missing \"";
                        }
                    } else
                    {
                        // non-quoted
                        if ( !isSymbol ( **str ))
                        {
                            throw "invalid json symbol value";
                        }
                        while ( isSymbolB ( **str ))
                        {
                            name += *((*str)++);
                        }
                    }

                    // skip whitespace
                    while ( isSpace ( **str ))
                        (*str)++;        // skip spaces and eol characters
                    // must have a :
                    if ((*str)[0] != ':' )
                    {
                        throw "missing name/value separator";
                    }
                    (*str)++;

                    // skip space after :
                    while ( isSpace ( **str ))
                        (*str)++;        // skip spaces and eol characters

                    // recurse for the value and just call the assignment operator for objects to do the assignment
                    obj[name] = jsonElement ( str );
                }
            } else if ((*str)[0] == '[' )
            {
                (*str)++;
                // instantiate us as an array
                value = jsonElement::arrayType ();

                // get a reference to our underlying array
                auto &arr = std::get<jsonElement::arrayType> ( value );

                bool first = true;
                // start looping, loop will terminate when we hit the end ] character
                for ( ;; )
                {
                    // eat any whitespace
                    while ( isSpace ( **str ))
                        (*str)++;        // skip spaces and eol characters

                    // test to see if we're done with the array
                    if ((*str)[0] == ']' )
                    {
                        (*str)++;
                        break;
                    }
                    // if we're not the first element than we should have a , separator
                    if ( !first )
                    {
                        if ((*str)[0] != ',' )
                        {
                            throw "missing comma";
                        }
                        (*str)++;
                        while ( isSpace ( **str ))
                            (*str)++;        // skip spaces and eol characters
                    }
                    first = false;

                    // recurse and push to the back of the array the jsonElement value
                    arr.push_back ( jsonElement ( str ));
                }
            } else if ((*str)[0] == '"' )
            {
                // we're a string
                (*str)++;

                std::string v;
                while ( **str && (*str)[0] != '"' )
                {
                    // handle any quoted special values
                    if ((*str)[0] == '\\' && (*str)[1] == '"' )
                    {
                        v += '"';
                        (*str) += 2;
                    } else if ((*str)[0] == '\\' && (*str)[1] == 'r' )
                    {
                        v += '\r';
                        (*str) += 2;
                    } else if ((*str)[0] == '\\' && (*str)[1] == 'n' )
                    {
                        v += '\n';
                        (*str) += 2;
                    } else if ((*str)[0] == '\\' && (*str)[1] == 't' )
                    {
                        v += '\t';
                        (*str) += 2;
                    } else if ((*str)[0] == '\\' && (*str)[1] )
                    {
                        v += (*str)[1];
                        (*str) += 2;
                    } else
                    {
                        v += *((*str)++);
                    }
                }
                // skip  over the ending " character
                if ( **str )
                {
                    (*str)++;
                } else
                {
                    throw "missing \"";
                }
                // assign us the parsed string
                value = std::move ( v );
            } else if ( isNumB ( **str ))
            {
                std::string v;
                bool isFloat = false;
                // loop over and build a string of all our number characters
                while ( isNum ( **str ))
                {
                    // if we see these we're a float
                    if ( **str == '.' )
                        isFloat = true;
                    if ( **str == 'e' )
                        isFloat = true;
                    v += *((*str)++);
                }
                // convert from string and assign us, either a float or a long long
                if ( isFloat )
                {
                    value = std::stod ( v.c_str ());
                } else
                {
                    value = std::stoll ( v.c_str ());
                }
            } else if ( !memcmp ( *str, "true", 4 ))
            {
                // boolean true
                value = true;
                *str += 4;
            } else if ( !memcmp ( *str, "false", 5 ))
            {
                // boolean false
                value = false;
                *str += 5;
            } else if ( !memcmp ( *str, "null", 4 ))
            {
                // null which is indicated by std::monostate in the variant
                value = std::monostate ();
                *str += 4;
            } else
            {
                throw "missing \"";
            }
        }

        //move constructor
        jsonElement ( jsonElement &&old )

        noexcept
        {
            *this = std::move ( old );
        }

        // copy constructor
        jsonElement ( jsonElement const &old )
        {
            value = old.value;
        }

        // copy operator
        jsonElement &operator= ( jsonElement const &old )
        {
            value = old.value;
            return *this;
        }

        // move operator
        jsonElement &operator= ( jsonElement &&old )

        noexcept
        {
            // free it an initialize it to the old type and copy old into us
            std::swap ( value, old.value );
            return *this;
        }

#if 1 // __cplusplus == 201703L
         template <typename T>
         using remove_cvref_t = typename std::remove_cv<typename std::remove_reference<T>::type>::type;

         template< class T, typename std::enable_if_t<((std::is_arithmetic_v<T> || std::is_enum_v<T>)) &&
                     !std::is_same_v<jsonElement, remove_cvref_t<T>>>* = nullptr>
#else
        // assignment operator for arithmetic types (bool, int, double)
        template< class T, typename std::enable_if_t<((std::is_arithmetic_v < T > || std::is_enum_v < T > )) && !std::is_same_v < jsonElement, typename std::remove_cvref_t<T>::type>> * = nullptr>
#endif

        jsonElement &operator= ( T const &v )
        {
            if constexpr ( std::is_same_v < bool, T > )
            {
                value = v;
            } else if constexpr ( std::is_integral_v < T > || std::is_enum_v < T > && !std::is_same_v < bool, T > )
            {
                value = (int64_t) v;
            } else if constexpr ( std::is_floating_point_v < T > )
            {
                value = (double) v;
            } else
            {
                value = v;
            }
            return *this;
        }

        // assignment operator for strings or string convertibles
        template< class T, typename std::enable_if_t<!std::is_arithmetic_v < T> && !std::is_enum_v <T> > * = nullptr>

        jsonElement &operator= ( T const &v )
        {
            value = std::string ( v );
            return *this;
        }

        // ----------------------------------------------- assignment methods

        // this returns a reference to an object with property name.     obj[std::string("name")]
        template< typename T, typename std::enable_if_t<std::is_same_v < T, std::string_view>> * = nullptr>

        jsonElement &operator[] ( T const &name )
        {
            if ( !std::holds_alternative<objectType> ( value ))
            {
                value = objectType ();
            }
            auto &obj = std::get<objectType> ( value );
            return obj[name];
        }

        // same as above except we take in a const char * as name rather than a std::string
        template< typename T, typename std::enable_if_t<std::is_same_v<T, const char *>> * = nullptr >
        jsonElement &operator[] ( T name )
        {
            if ( !std::holds_alternative<objectType> ( value ))
            {
                value = objectType ();
            }
            auto &obj = std::get<objectType> ( value );
            return obj[std::string ( name )];
        }

        // array dereference operator, returns a reference to the <index> element (0-based).    obj[<index>]
        template< typename T, typename std::enable_if_t<std::is_integral_v < T>> * = nullptr>

        jsonElement &operator[] ( T index )
        {
            if ( !std::holds_alternative<arrayType> ( value ))
            {
                value = arrayType ();
            }
            auto &arr = std::get<arrayType> ( value );
            if ((size_t) index == arr.size ())
            {
                if ( arr.capacity () <= (size_t) index + 1 )
                {
                    arr.reserve ((size_t) index + 256 );
                }
                arr.resize ((size_t) index + 1 );
            }
            return arr[index];
        }

        // allows for emplacement of a new value for an array jsonElement
        template< typename ...T >
        void emplace_back ( T &&...t )
        {
            if ( !std::holds_alternative<arrayType> ( value ))
            {
                value = arrayType ();
            }
            auto &arr = std::get<arrayType> ( value );
            arr.emplace_back ( std::forward<T...> ( t... ));
        }

        // reference accessors
        operator bool & ()
        {
            if ( std::holds_alternative<int64_t> ( value ))
            {
                value = std::get<int64_t> ( value ) ? true : false;
            } else if ( !std::holds_alternative<bool> ( value ))
            {
                value = false;
            }
            return std::get<bool> ( value );
        }

        operator int64_t & ()
        {
            if ( std::holds_alternative<double> ( value ))
            {
                value = (int64_t) std::get<double> ( value );
            }
            if ( !std::holds_alternative<int64_t> ( value ))
            {
                value = (int64_t) 0;
            }
            return std::get<int64_t> ( value );
        }

        operator double & ()
        {
            if ( std::holds_alternative<int64_t> ( value ))
            {
                value = (double) std::get<int64_t> ( value );
            } else if ( !std::holds_alternative<double> ( value ))
            {
                value = 0.0;
            }
            return std::get<double> ( value );
        }

        operator std::string & ()
        {
            if ( std::holds_alternative<int64_t> ( value ))
            {
                value = (double) std::get<int64_t> ( value );
            } else if ( !std::holds_alternative<std::string> ( value ))
            {
                value = std::string ();
            }
            return std::get<std::string> ( value );
        }

        // resets the jsonElement to monostate (no valid state)
        void clear ()
        {
            value = std::monostate ();
        }

        // turns the jsonElement into a 0-length array (we ended up with a [] being emitted upon serialization)
        jsonElement &makeArray ()
        {
            if ( std::holds_alternative<arrayType> ( value ))
            {
            } else if ( std::holds_alternative<std::monostate> ( value ))
            {
                value = arrayType ();
            } else
            {
                throw "cannot be made an array";
            }
            return *this;
        }

        // turns the jsonElement into an object with no elements (a {} will be emitted upon serialization)
        jsonElement &makeObject ()
        {
            if ( std::holds_alternative<objectType> ( value ))
            {
            }
            if ( std::holds_alternative<std::monostate> ( value ))
            {
                value = objectType ();
            } else
            {
                throw "cannot be made an array";
            }
            return *this;
        }

        // ------------------------------------- reader functions

        // used to test to see if an object contains a specific named element
        bool has ( std::string_view const &name ) const
        {
            if ( std::holds_alternative<objectType> ( value ))
            {
                auto &obj = std::get<objectType> ( value );
                auto it = obj.find ( name );
                if ( it != obj.end ())
                {
                    if ( std::holds_alternative<std::monostate> ( it->second.value ))
                    {
                        return false;
                    }
                    return true;
                }
                return false;
            }
            return false;
        }

        // constant returned reference for the indexed value of a jsonElement array
        template< typename T, typename std::enable_if_t<std::is_integral_v < T>> * = nullptr>

        jsonElement const &operator[] ( T index ) const
        {
            if ( std::holds_alternative<arrayType> ( value ))
            {
                auto &arr = std::get<arrayType> ( value );
                if ((size_t) index < arr.size ())
                {
                    return arr[(size_t) index];
                }
                throw "element not found";
            }
            throw "element not found";
        }

        // constant returned reference for the std::string(<named>) value of the jsonElement object
        template< typename T, typename std::enable_if_t<std::is_same_v < T, std::string_view>> * = nullptr>

        jsonElement const &operator[] ( T const &name ) const
        {
            if ( std::holds_alternative<objectType> ( value ))
            {
                auto &obj = std::get<objectType> ( value );
                auto it = obj.find ( name );
                if ( it != obj.end ())
                {
                    if ( std::holds_alternative<std::monostate> ( it->second.value ))
                    {
                        throw "element not found";
                    }
                    return it->second;
                }
                throw "element not found";
            }
            throw "element not found";
        }

        // constant returned reference for the (const char *)(<named>) value of the jsonElement object
        template< typename T, typename std::enable_if_t<std::is_same_v < T, char const *>> * = nullptr>

        auto &operator[] ( T name ) const
        {
            return (*this)[std::string_view ( name )];
        }

        // push a value to the back of a jsonElement array
        void push_back ( jsonElement const &elem )
        {
            makeArray ();
            auto &arr = std::get<arrayType> ( value );
            arr.push_back ( elem );
        }

        // reserve size elements in the jsonElement array
        jsonElement &reserve ( size_t size )
        {
            if ( std::holds_alternative<arrayType> ( value ))
            {
            } else if ( std::holds_alternative<std::monostate> ( value ))
            {
                value = arrayType ();
            } else
            {
                throw "cannot be made an array";
            }
            auto &arr = std::get<arrayType> ( value );
            arr.reserve ( size );

            return *this;
        }

        // constant begin iterator for jsonElement object
        auto cbeginObject () const
        {
            if ( std::holds_alternative<objectType> ( value ))
            {
                auto &obj = std::get<objectType> ( value );
                return obj.cbegin ();
            }
            throw "json iterating over not object";
        }

        // constant end iterator for jsonElementObject
        auto cendObject () const
        {
            if ( std::holds_alternative<objectType> ( value ))
            {
                auto &obj = std::get<objectType> ( value );
                return obj.cend ();
            }
            throw "json iterating over not object";
        }

        // constant begin iterator for jsonElement array
        auto cbeginArray () const
        {
            if ( std::holds_alternative<arrayType> ( value ))
            {
                auto &arr = std::get<arrayType> ( value );
                return arr.cbegin ();
            }
            throw "json iterating over non array";
        }

        // constant end iterator for jsonElement array
        auto cendArray () const
        {
            if ( std::holds_alternative<arrayType> ( value ))
            {
                auto &arr = std::get<arrayType> ( value );
                return arr.cend ();
            }
            throw "json iterating over non array";
        }

        // prototype for the value accessors
        operator int64_t const () const
        {
            if ( std::holds_alternative<int64_t> ( value ))
            {
                return std::get<int64_t> ( value );
            }
            throw "invalid json integer value";
        }

        operator bool const () const
        {
            if ( std::holds_alternative<bool> ( value ))
            {
                return std::get<bool> ( value );
            }
            throw "invalid json integer value";
        }

        operator double const () const
        {
            if ( std::holds_alternative<double> ( value ))
            {
                return std::get<double> ( value );
            }
            throw "invalid json double value";
        }

        operator std::string const & () const
        {
            if ( std::holds_alternative<std::string> ( value ))
            {
                return std::get<std::string> ( value );
            }
            throw "invalid json string value";
        }

        size_t size () const
        {
            if ( std::holds_alternative<objectType> ( value ))
            {
                auto &obj = std::get<objectType> ( value );
                return obj.size ();
            } else if ( std::holds_alternative<arrayType> ( value ))
            {
                auto &arr = std::get<arrayType> ( value );
                return arr.size ();
            } else if ( std::holds_alternative<std::monostate> ( value ))
            {
                return 0;
            }
            throw "invalid usage";
        }

        // testers.  pretty self-explanatory
        bool isNull () const
        {
            if ( std::holds_alternative<std::monostate> ( value ))
            {
                return true;
            } else
            {
                return false;
            }
        }

        bool isArray () const
        {
            if ( std::holds_alternative<arrayType> ( value ))
            {
                return true;
            } else
            {
                return false;
            }
        }

        bool isObject () const
        {
            if ( std::holds_alternative<objectType> ( value ))
            {
                return true;
            } else
            {
                return false;
            }
        }

        bool isInteger () const
        {
            if ( std::holds_alternative<int64_t> ( value ))
            {
                return true;
            } else
            {
                return false;
            }
        }

        bool isDouble () const
        {
            if ( std::holds_alternative<double> ( value ))
            {
                return true;
            } else
            {
                return false;
            }
        }

        bool isString () const
        {
            if ( std::holds_alternative<std::string> ( value ))
            {
                return true;
            } else
            {
                return false;
            }
        }

        bool isBool () const
        {
            if ( std::holds_alternative<bool> ( value ))
            {
                return true;
            } else
            {
                return false;
            }
        }

        // ------------------------------- serialization
        // turns jsonElement's into a json string.
        // if quoteNames controls whether the name of an object value is quoted   ie.  "name" : value
        void serialize ( std::string &buff, bool quoteNames ) const
        {
            if ( std::holds_alternative<objectType> ( value ))
            {
                auto &obj = std::get<objectType> ( value );
                buff.push_back ( '{' );
                bool first = true;
                for ( auto &&[name, v]: obj )
                {
                    if ( !first )
                    {
                        buff.push_back ( ',' );
                    }
                    first = false;
                    if ( quoteNames )
                        buff.push_back ( '\"' );
                    buff.append ( name );
                    if ( quoteNames )
                        buff.push_back ( '\"' );
                    buff.push_back ( ':' );
                    v.serialize ( buff, quoteNames );
                }
                buff.push_back ( '}' );
            } else if ( std::holds_alternative<arrayType> ( value ))
            {
                auto &arr = std::get<arrayType> ( value );
                buff.push_back ( '[' );
                bool first = true;
                for ( auto &it: arr )
                {
                    if ( !first )
                    {
                        buff.push_back ( ',' );
                    }
                    first = false;
                    it.serialize ( buff, quoteNames );
                }
                buff.push_back ( ']' );
            } else if ( std::holds_alternative<int64_t> ( value ))
            {
                auto v = std::get<int64_t> ( value );
                buff.append ( std::to_string ( v ));
            } else if ( std::holds_alternative<double> ( value ))
            {
                auto v = std::get<double> ( value );
                buff.append ( std::to_string ( v ));
            } else if ( std::holds_alternative<std::string> ( value ))
            {
                auto &v = std::get<std::string> ( value );
                buff.push_back ( '\"' );
                for ( auto &it: v )
                {
                    switch ( it )
                    {
                        case '\"':
                            buff.append ( "\\\"", 2 );
                            break;
                        case '\\':
                            buff.append ( "\\\\", 2 );
                            break;
                        case '\r':
                            buff.append ( "\\r", 2 );
                            break;
                        case '\n':
                            buff.append ( "\\n", 2 );
                            break;
                        case '\t':
                            buff.append ( "\\t", 2 );
                            break;
                        default:
                            if ( it < 32 || it > 127 )
                            {
                                buff.push_back ( '%' );
                                buff.push_back ( "0123456789ABCDEF"[(it & 0xF0) >> 4] );
                                buff.push_back ( "0123456789ABCDEF"[(it & 0x0F)] );
                            } else
                            {
                                buff.push_back ( it );
                            }
                    }
                }
                buff.push_back ( '\"' );
            } else if ( std::holds_alternative<bool> ( value ))
            {
                if ( std::get<bool> ( value ))
                {
                    buff.append ( "true", 4 );
                } else
                {
                    buff.append ( "false", 5 );
                }
            } else if ( std::holds_alternative<std::monostate> ( value ))
            {
                buff.append ( "null", 4 );
            }
        }

        // helper methods for the json parser
        static bool isSpace ( char const c )
        {
            if ((c == ' ') || (c == '\t') or (c == '\r') or (c == '\n'))
            {
                return true;
            }
            return false;
        }

        static bool isSymbolB ( char const c )
        {
            if (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || (c == '_'))
            {
                return true;
            }
            return false;
        }

        static bool isNum ( char const c )
        {
            if (((c >= '0') && (c <= '9')) || (c == '+') || (c == '-'))
            {
                return true;
            }
            return false;
        }

        static bool isNumB ( char const c )
        {
            if ( isNum ( c ) || (c == 'e'))
            {
                return true;
            }
            return false;
        }

        static bool isSymbol ( char const c )
        {
            if ( isSymbolB ( c ) || ((c >= '0') && (c <= '9')))
            {
                return true;
            }
            return false;
        }
    };

    jsonElement jsonParser ( char const *str )
    {
        auto tmpStr = &str;
        auto result = jsonElement ( tmpStr );
        while ( jsonElement::isSpace ( **tmpStr ))
            (*tmpStr)++;        // skip spaces and eol characters
        if ( **tmpStr )
        {
            throw "invalid json";
        }
        return result;
    }
};
