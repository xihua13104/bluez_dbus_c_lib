# XML for gdbus-codegen
DBUS_XML = src/bluez_iface.xml
DBUS_GEN_C = src/bluez_iface.c
DBUS_GEN_H = src/bluez_iface.h
DBUS_CODEGEN = gdbus-codegen

# Auto-generate bluez_iface.c from XML
$(DBUS_GEN_C): $(DBUS_XML)
	@echo "Generating D-Bus code from $(DBUS_XML)..."
	$(DBUS_CODEGEN) --output $@ --interface-info-body --interface-prefix org $<
	@echo "==> Done generating $@"

# Auto-generate bluez_iface.h from XML
$(DBUS_GEN_H): $(DBUS_XML)
	@echo "Generating D-Bus header from $(DBUS_XML)..."
	$(DBUS_CODEGEN) --output $@ --interface-info-header --interface-prefix org $<
	@echo "==> Done generating $@"

# Group header and source generation into one target
.PHONY: dbus-gen
dbus-gen: $(DBUS_GEN_C) $(DBUS_GEN_H)

# Define the APP source files
APP_SOURCES = \
	main.c

# Define the LIB source files
LIB_SOURCES = \
	$(DBUS_GEN_C) \
	src/lm_log.c \
	src/lm.c \
	src/lm_adapter.c \
	src/lm_device.c \
	src/lm_adv.c \
	src/lm_agent.c \
	src/lm_player.c \
	src/lm_transport.c \
	src/lm_utils.c

# Ensure generated files are ready before compiling
$(APP_OBJ) $(LIB_OBJ): $(DBUS_GEN_C) $(DBUS_GEN_H)

# Convert source files to object files
APP_OBJ = $(APP_SOURCES:.c=.o)
LIB_OBJ = $(LIB_SOURCES:.c=.o)

# Include paths
INCLUDES = -I$(STAGING_DIR)/usr/include/bluez \
		-I$(STAGING_DIR)/usr/include/glib-2.0 \
		-I$(STAGING_DIR)/usr/lib/glib-2.0/include \
		-I$(STAGING_DIR)/usr/include/dbus-1.0 \
		-I$(STAGING_DIR)/usr/lib/dbus-1.0/include \
		-I$(STAGING_DIR)/usr/include/libxml2 \
		-I$(STAGING_DIR)/usr/include \
	-Iinc

# Libraries to link against
LIBS = -lpthread -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lm -lxml2 -lbluetooth

# Compilation flags
CFLAGS = -Wall -Wextra $(INCLUDES) -fpermissive -fPIC

# Linker flags
LDFLAGS = $(LIBS)

APP_TARGET = lea_manager
LIB_TARGET = liblea_manager.so

# Default rule: build both targets
# Default rule: build both targets
all: dbus-gen $(APP_TARGET) $(LIB_TARGET)

# Build the app executable
$(APP_TARGET): $(APP_OBJ) $(LIB_TARGET)
	$(CC) $(APP_OBJ) $(LIB_OBJ) -o $@ $(LDFLAGS) -L. -l:$(LIB_TARGET)

# Build the shared library
$(LIB_TARGET): $(LIB_OBJ)
	$(CC) -shared -o $@ $(LIB_OBJ)

# Rule for object file compilation
%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

# Clean up build files
.PHONY: clean

clean:
	rm -f $(APP_OBJ) $(LIB_OBJ) $(APP_TARGET) $(LIB_TARGET) $(DBUS_GEN_C) $(DBUS_GEN_H)
