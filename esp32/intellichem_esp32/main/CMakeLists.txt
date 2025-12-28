# Main component CMakeLists.txt

idf_component_register(
    SRCS
        "main.c"
        "protocol/message.c"
        "protocol/buffer.c"
        "protocol/parser.c"
        "serial/rs485.c"
        "serial/serial_task.c"
    INCLUDE_DIRS
        "."
        "protocol"
        "models"
        "serial"
)
