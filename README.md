# fb303
fb303 is a core set of thrift functions that provide a common mechanism for
querying stats and other information from a service.

## Dependencies
fb303 depends on
* [folly](https://github.com/facebook/folly)
* folly's own prerequisites: double-conversion and glog
* [fbthrift](https://github.com/facebook/fbthrift)

## Using fb303 for your service
You can have your own thrift service interface extend `fb303.FacebookService` to
utilize the fb303 API. Some of the primary interfaces defined by fb303 are:
* `getStatus()` - Query the state of a running service.
* `getCounters()` - Get custom statistics about the performance and behavior of
a service.
* `getExportedValues()` - Get arbitrary string values exported by a service.
This can be used to export things such as the build metadata (version info and
the source control commit ID it was built from) and other key configuration
information.
* `getOptions()` / `setOptions()` - Get and set configurable service parameters.

The FacebookBase implementation for C++ will be released shortly, with other
languages to follow, but until then you will need to write your own
implementations of the fb303 APIs.

## Curious about the name?
Wikipedia: http://en.wikipedia.org/wiki/Roland_TB-303

The TB303 makes bass lines.
.Bass is what lies underneath any strong tune.
..fb303 is the shared root of all thrift services.
...fb303 => FacebookBase303.

## Join the fb303 community
See the CONTRIBUTING file for how to help out.

## License
fb303 is BSD-licensed. We also provide an additional patent grant.