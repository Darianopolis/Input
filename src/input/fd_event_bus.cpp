#include "fd_event_bus.hpp"

#include "core.hpp"

#include <memory>
#include <list>

#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

namespace input
{
    struct FdEventHandler
    {
        int fd;
        FdEventCallback callback;
    };

    struct FdEventBus::Impl : FdEventBus {
        int epollfd = -1;
        std::list<FdEventHandler> handlers;
    };

    FdEventBus* FdEventBus::create()
    {
        auto bus = new FdEventBus::Impl;
        defer { unref(bus); };
        bus->epollfd = epoll_create1(0);
        return take(bus);
    }

    void FdEventBus::destroy(FdEventBus* _self)
    {
        decl_self(_self);

        close(self->epollfd);
        delete self;
    }

    void FdEventBus::register_fd_listener(int fd, uint32_t events, FdEventCallback&& fn)
    {
        decl_self(this);

        epoll_event event {
            .events = events,
            .data{.ptr = &self->handlers.emplace_back(fd, fn)},
        };
        unix_check_n1(epoll_ctl(self->epollfd, EPOLL_CTL_ADD, fd, &event));
    }

    void FdEventBus::unregister_fd_listener(int fd)
    {
        decl_self(this);

        auto iter = std::ranges::find_if(self->handlers, [&](auto& handler) { return handler.fd == fd; });
        if (iter == self->handlers.end()) {
            log_warn("File descriptor {} not found in registered list", fd);
            return;
        }

        unix_check_n1(epoll_ctl(self->epollfd, EPOLL_CTL_DEL, iter->fd, nullptr));
        self->handlers.erase(iter);

        log_debug("Successfully unregistered file descriptor: {}", fd);
    }

    void FdEventBus::run()
    {
        decl_self(this);

        for (;;) {
            epoll_event events[16];
            auto events_ready = unix_check_n1(epoll_wait(self->epollfd, events, std::size(events), -1), EINTR);
            if (events_ready <= 0) continue;

            for (int i = 0; i < events_ready; ++i) {
                auto* handler = static_cast<FdEventHandler*>(events[i].data.ptr);
                handler->callback(FdEventData {
                    .fd = handler->fd,
                    .events = events[i].events
                });
            }
        }
    }
}
