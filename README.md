# co_reactor

[Reactor pattern](https://en.wikipedia.org/wiki/Reactor_pattern) implementation using C++ Coroutines.  
Coroutine return values and error handling is omitted for sake of simplicity.

#### Reactor::run
Run the event loop

#### Reactor::event
Wait for some event on a file descriptor

#### co_spawn
Spawn a new coroutine
