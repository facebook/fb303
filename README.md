# fb303

fb303 is a base [Thrift](https://github.com/facebook/fbthrift) service
and a common set of functionality for querying stats, options, and
other information from a service.

## Dependencies

fb303 depends on
* [folly](https://github.com/facebook/folly)
* [gflags](https://gflags.github.io/gflags/)
* [glog](https://github.com/google/glog)
* [fbthrift](https://github.com/facebook/fbthrift)

And all transitive dependencies of the above.

## Using fb303 for your service

You can have your own thrift service interface extend `fb303_core.BaseService` to
utilize the fb303 API. Some of the primary interfaces defined by fb303 are:
* `getStatus()` - Query the state of a running service.
* `getCounters()` - Get custom statistics about the performance and behavior of
a service.
* `getExportedValues()` - Get arbitrary string values exported by a service.
This can be used to export things such as the build metadata (version info and
the source control commit ID it was built from) and other key configuration
information.
* `getOptions()` / `setOption()` - Get and set configurable service parameters.

C++ service handler implementations can extend `facebook::fb303::BaseService` for
default implementations of the Thrift methods.

## Curious about the name?

Wikipedia: http://en.wikipedia.org/wiki/Roland_TB-303

The TB-303 makes bass lines.

Bass is what lies underneath any strong tune.

fb303 is the shared root of all Thrift services.

fb303 ⇒ FacebookBase303.

## Join the fb303 community

See the CONTRIBUTING file for how to help out.

## License

fb303 is licensed under Apache 2.0 as found in the LICENSE file.

## Note

fb303 is a dependency of many other projects at Facebook which expose
Thrift interfaces; some which have been open-sourced. Examples include
[edenfs](https://github.com/facebookexperimental/eden),
[proxygen](https://github.com/facebook/proxygen), and
[fboss](https://github.com/facebook/fboss).

This project has evolved over many years, and parts of it predate
C++11. While it provides useful functionality for service information
reporting, it is not representative of modern C++ coding standards.

An early version of fb303 was originally open-sourced in the Apache
Thrift project in 2008, and still exists in the Apache Thrift codebase
under
[contrib/fb303/](https://github.com/apache/thrift/tree/master/contrib/fb303).
The fb303 code has since continued evolving at Facebook.  This version
contained in this repository has significantly expanded functionality
with regards to statistics tracking and reporting in C++ services.
