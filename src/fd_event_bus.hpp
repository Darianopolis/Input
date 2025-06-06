#pragma once

#include "core.hpp"

#include <functional>

#include <sys/epoll.h>

namespace input
{
    struct FdEventData
    {
        int fd;
        uint32_t events;
    };

    using FdEventCallback = std::function<void(FdEventData)>;

    struct FdEventBus : RefCounted
    {
        struct Impl;

        static FdEventBus* create();
        static void destroy(FdEventBus*);

    public:
        void register_fd_listener(int fd, uint32_t events, FdEventCallback&& callback);
        void unregister_fd_listener(int fd);
        void run();
    };
}
