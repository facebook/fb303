add_thrift_cpp2_library(
  fb303_thrift_cpp2
  fb303_core.thrift
  SERVICES
    BaseService
)

install(
  FILES fb303_core.thrift
  DESTINATION thrift
)
