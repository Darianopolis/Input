#pragma once

#include <utility>
#include <format>
#include <iostream>
#include <stdexcept>
#include <print>

using namespace std::literals;

#include <string.h>

#define begin_namespace namespace input {
#define begin_anonymous } namespace { using namespace input;
#define end_anonymous } namespace input {
#define end_namespace }

namespace input
{
    template<typename Fn>
    struct Defer
    {
        Fn fn;

        Defer(Fn&& fn): fn(std::move(fn)) {}
        ~Defer() { fn(); };
    };

#define defer Defer _ = [&]

// -----------------------------------------------------------------------------

#define text_ansi_color(color, text) "\u001B[" #color "m" text "\u001B[0m"

    template<typename ...Args>
    void log_trace(std::format_string<Args...> fmt, Args&&... args)
    {
        std::println("[" text_ansi_color(90, "TRACE") "] " text_ansi_color(90, "{}"), std::vformat(fmt.get(), std::make_format_args(args...)));
    }

    template<typename ...Args>
    void log_debug(std::format_string<Args...> fmt, Args&&... args)
    {
        std::println("[" text_ansi_color(96, "DEBUG") "] {}", std::vformat(fmt.get(), std::make_format_args(args...)));
    }

    template<typename ...Args>
    void log_info(std::format_string<Args...> fmt, Args&&... args)
    {
        std::println(" [" text_ansi_color(94, "INFO") "] {}", std::vformat(fmt.get(), std::make_format_args(args...)));
    }

    template<typename ...Args>
    void log_warn(std::format_string<Args...> fmt, Args&&... args)
    {
        std::println(" [" text_ansi_color(93, "WARN") "] {}", std::vformat(fmt.get(), std::make_format_args(args...)));
    }

    template<typename ...Args>
    void log_error(std::format_string<Args...> fmt, Args&&... args)
    {
        std::println("[" text_ansi_color(91, "ERROR") "] {}", std::vformat(fmt.get(), std::make_format_args(args...)));
    }

// -----------------------------------------------------------------------------

    template<typename ...Args>
    [[noreturn]]
    void raise_error(std::format_string<Args...> fmt, Args&&... args)
    {
        auto message = std::vformat(fmt.get(), std::make_format_args(args...));
        log_error("{}", message);
        throw std::runtime_error(message);
    }

    [[noreturn]]
    inline
    void raise_unix_error(std::string_view message, int err = 0)
    {
        err = err ?: errno;

        if (message.empty()) raise_error("({}) {}",              err, strerror(err));
        else                 raise_error("{}: ({}) {}", message, err, strerror(err));
    }

    enum class UnixErrorBehaviour {
        RetNull,
        RetNeg1,
        RetNegErrno,
        CheckErrno,
    };

    template<typename T>
    T unix_check_(T res, UnixErrorBehaviour check, auto... allowed)
    {
        bool error_occured = false;
        int error_code = 0;

        switch (check) {
            break;case UnixErrorBehaviour::RetNull:     if (!res)      { error_occured = true; error_code = errno; }
            break;case UnixErrorBehaviour::RetNeg1:     if (res == -1) { error_occured = true; error_code = errno; }
            break;case UnixErrorBehaviour::RetNegErrno: if (res < 0)   { error_occured = true; error_code = -res;  }
            break;case UnixErrorBehaviour::CheckErrno:  if (errno)     { error_occured = true; error_code = errno; }
        }

        if (!error_occured || (... || (error_code == allowed))) return res;

        raise_unix_error("unix_check", error_code);
    }

#define unix_check_null(func, ...)                       unix_check_((func), UnixErrorBehaviour::RetNull     __VA_OPT__(,) __VA_ARGS__)
#define unix_check_n1(func, ...)                         unix_check_((func), UnixErrorBehaviour::RetNeg1     __VA_OPT__(,) __VA_ARGS__)
#define unix_check_ne(func, ...)                         unix_check_((func), UnixErrorBehaviour::RetNegErrno __VA_OPT__(,) __VA_ARGS__)
#define unix_check_ce(func, ...) [&] { errno = 0; return unix_check_((func), UnixErrorBehaviour::CheckErrno  __VA_OPT__(,) __VA_ARGS__); }()

// -----------------------------------------------------------------------------

    struct RefCounted
    {
        uint32_t ref_count = 1;

        template<std::derived_from<RefCounted> T>
        friend auto* ref(T* v)
        {
            v->ref_count++;
            return v;
        }

        template<std::derived_from<RefCounted> T>
        friend void unref(T* v)
        {
            if (!v) return;
            if (!--v->ref_count) {
                T::destroy(v);
            }
        }

        template<std::derived_from<RefCounted> T>
        friend T* take(T*& v)
        {
            T* t = v;
            v = nullptr;
            return t;
        }
    };

    inline
    int take_fd(int& fd)
    {
        int _fd = fd;
        fd = -1;
        return _fd;
    }

#define get_impl(var) static_cast<std::remove_cvref_t<decltype(*var)>::Impl*>(var)

#define decl_self_nullable(var) auto* self = get_impl(var)
#define decl_self(var) decl_self_nullable(var); if (!self) raise_error("self: nullptr")
}
