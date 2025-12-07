#pragma once

#include <map>
#include <string>

#include <boost/json.hpp>

template<class T>
struct DoubleMap {
    std::map<std::string, T> strToType;
    std::map<T, std::string> typeToStr;
    DoubleMap(const std::map<T, std::string> & enumMap) {
        for(const auto & [key, value]: enumMap) {
            typeToStr[key] = value;
            strToType[value] = key;
        }
    }
    T operator[](const std::string & key)
    {
        if(!strToType.contains(key))
            return T(0);

        return T(strToType[key]);
    }

    T operator[](const char * key)
    {
        if(!strToType.contains(key))
            return T(0);

        return T(strToType[key]);
    }

    T operator[](const boost::json::value & value)
    {
        if(value.is_null())
            return T(0);

        if(!value.is_string())
            return T(0);

        std::string key = value.as_string().c_str();

        if(!strToType.contains(key))
            return T(0);

        return T(strToType[key]);
    }

    std::string operator[](const T key)
    {
        if(!typeToStr.contains(key))
            return typeToStr.begin()->second;
        return typeToStr[key];
    }
};