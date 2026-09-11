# fb303

fb303 is the standard base service library used across Facebook services. It defines the fb303 Thrift interface and the C++ building blocks that implement it, providing common monitoring and management functionality: exported counters, stats, histograms and timeseries, service status, and other runtime introspection that every service exposes.

Files in this folder are the core fb303 C++ sources and headers, the Thrift definitions, OSS mirroring support, and their tests. Ownership is declared in the BUCK file via the thrift oncall.
