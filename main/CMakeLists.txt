set(COMPONENT_SRCS "app_main.c" "../isotp/isotp.c" "../isotp/send.c" "../isotp/receive.c" "../bitfield/bitfield.c" "../bitfield/bitarray.c" "../bitfield/8byte.c")
set(COMPONENT_ADD_INCLUDEDIRS "." "..")


register_component()
