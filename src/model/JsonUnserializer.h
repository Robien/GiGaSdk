/*
 * JsonUnserializer.h
 *
 *  Created on: 30 sept. 2015
 *      Author: thomas
 */

#ifndef JSONUNSERIALIZER_H_
#define JSONUNSERIALIZER_H_

#include <cpprest/json.h>
#include <boost/optional.hpp>

namespace giga {

class JSonUnserializer;

namespace details {
    inline int getValue(const web::json::value& value, int&) {
        return value.as_integer();
    }
    inline bool getValue(const web::json::value& value, bool&) {
        return value.as_bool();
    }
    inline double getValue(const web::json::value& value, double&) {
        return value.as_double();
    }
    inline std::string getValue(const web::json::value& value, std::string&) {
        return value.as_string();
    }
    inline const char* getValue(const web::json::value& value, const char*&) {
        return value.as_string().c_str();
    }

    template <typename T>
    T getValue(const web::json::value& value, T&) {
        auto elm = T{};
        elm.visit(JSonUnserializer{value});
        return elm;
    }

    template <typename T>
    std::unique_ptr<T> getValue(const web::json::value& value, std::unique_ptr<T>&) {
        if (value.is_null()) {
            return nullptr;
        } else {
            auto t = std::unique_ptr<T>(new T{});
            details::getValue(value, *t);
            return t;
        }
    }

    template <typename T>
    std::vector<T> getValue(const web::json::value& value, std::vector<T>&) {
        auto values = value.as_array();
        auto ret = std::vector<T>();
        ret.reserve(values.size());
        for(auto value : values) {
            auto t = T{};
            ret.push_back(getValue(value, t));
        }
        return ret;
    }

    template <typename T>
    boost::optional<T> getValue(const web::json::value& value, boost::optional<T>&) {
        if (value.is_null()) {
            return boost::none;
        } else {
            auto t = T{};
            return boost::make_optional(details::getValue(value, t));
        }
    }
} // namespace details

class JSonUnserializer final
{
public:

    JSonUnserializer (const web::json::value& val) :
            val (val)
    {
    }

    template <typename T> T unserialize() const {
        return doUnserialize(T{});
    }
    template <typename T> T doUnserialize(T) const {
        T data{};
        data.visit(*this);
        return data;
    }
    template <typename T> std::unique_ptr<T> doUnserialize(std::unique_ptr<T>) const {
        auto data = std::unique_ptr<T>(new T{});
        data->visit(*this);
        return std::move(data);
    }

    template <typename T> std::shared_ptr<T> doUnserialize(std::shared_ptr<T>) const {
        auto data = std::make_shared<T>();
        data->visit(*this);
        return std::move(data);
    }

    template <typename T> void manageOpt(T& current, const std::string& name, T defaultValue) const {
        if (val.has_field(name)) {
            current = details::getValue(val.at(name), current);
        } else {
            current = defaultValue;
        }
    }
    template <typename T> void manage(T& current, std::string name) const {
        current = details::getValue(val.at(name), current);
    }

    template <typename T> void manage(boost::optional<T>& current, const std::string& name) const {
        if (!val.has_field(name)) {
            current = boost::none;
        } else {
            current = details::getValue(val.at(name), current);
        }
    }
    template <typename T> void manage(std::unique_ptr<T>& current, const std::string& name) const {
        if (!val.has_field(name)) {
            current = nullptr;
        } else {
            current = details::getValue(val.at(name), current);
        }
    }

private:
    const web::json::value& val;

};

} // namespace giga
#endif /* JSONUNSERIALIZER_H_ */
