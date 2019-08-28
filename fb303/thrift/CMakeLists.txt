add_thrift_cpp2_library(
  fb303_thrift_cpp
  fb303_core.thrift
  THRIFT_INCLUDE_DIR "${INCLUDE_INSTALL_DIR}/thrift-files"
  SERVICES
    BaseService
)

install(
  TARGETS fb303_thrift_cpp
  EXPORT fb303-exports
  LIBRARY DESTINATION "${LIB_INSTALL_DIR}"
  ARCHIVE DESTINATION "${LIB_INSTALL_DIR}"
  PUBLIC_HEADER DESTINATION
    "$<TARGET_PROPERTY:fb303_thrift_cpp,HEADER_INSTALL_DIR>"
)
install(
  TARGETS fb303_thrift_cpp.thrift_includes
  EXPORT fb303-exports
)
install(
  FILES fb303_core.thrift
  DESTINATION "$<TARGET_PROPERTY:fb303_thrift_cpp,THRIFT_INSTALL_DIR>"
)
