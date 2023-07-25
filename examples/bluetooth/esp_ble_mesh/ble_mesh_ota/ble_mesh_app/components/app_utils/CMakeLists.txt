
set(COMPONENT_SRCS "app_utils.c")

set(COMPONENT_ADD_INCLUDEDIRS ". include")

# requirements can't depend on config
set(COMPONENT_REQUIRES example_nvs app_update)

register_component()
