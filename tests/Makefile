CC ?= cc
BUILD := ../../../sim/build/PrinterDemo/tests
CPPFLAGS := -I../include
CFLAGS := -std=c99 -g -Wall -Wextra -Werror -pedantic
SIMFLAGS := -I../../../sim -I../../../sim/include -I../../../sim/lvgl \
	-I../../../toolchain/app_c_sdk/include -DLV_CONF_INCLUDE_SIMPLE \
	-I../../../app/NetzmeSB/src -I../../../app/NetzmeSB/src/libs/logger \
	-I../../../app/NetzmeSB/src/libs/init -I../../../app/NetzmeSB/include \
	-DLV_LVGL_H_INCLUDE_SIMPLE -DCFG_APP_PATH=\"/home/user0/PrinterDemo/\" -DSIM_BUILD

.PHONY: check check-menus link-fsroot
check: link-fsroot $(BUILD)/test_runner $(BUILD)/transport_test $(BUILD)/qr_request_test $(BUILD)/qris_tap_test $(BUILD)/mqtt_test $(BUILD)/mqtt_message_test $(BUILD)/ui_test $(BUILD)/soundbox_test $(BUILD)/soundbox_full_test $(BUILD)/store_test $(BUILD)/backend_test check-menus
	$(BUILD)/test_runner
	$(BUILD)/transport_test
	$(BUILD)/qr_request_test
	$(BUILD)/qris_tap_test
	$(BUILD)/mqtt_test
	$(BUILD)/mqtt_message_test
	$(BUILD)/ui_test
	$(BUILD)/soundbox_test
	$(BUILD)/soundbox_full_test
	$(BUILD)/store_test
	$(BUILD)/backend_test

$(BUILD)/qris_tap_test: qris_tap_test.c ../src/qris_tap.c ../include/qris_tap.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(SIMFLAGS) $(CFLAGS) qris_tap_test.c ../src/qris_tap.c -o $@

$(BUILD)/backend_test: backend_test.c ../src/soundbox_backend.c ../src/soundbox_store.c ../src/qris_tap.c ../include/soundbox_backend.h ../include/qris_tap.h ../config/conf.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(SIMFLAGS) -include ../config/conf.h \
	    -std=gnu99 -Wall -Wextra -Werror -Wno-error=comment -ffunction-sections \
	    backend_test.c ../src/soundbox_backend.c ../src/soundbox_store.c ../src/qris_tap.c \
	    ../../../sim/include/cJSON.c -o $@ -lm -Wl,--gc-sections

$(BUILD)/test_runner: test_runner.c ../src/print_jobs.c ../include/print_jobs.h ../../../sim/lvgl/src/extra/libs/qrcode/qrcodegen.c ../../../toolchain/sdk/include/lvgl/src/extra/libs/qrcode/qrcodegen.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) -I../../../toolchain/sdk/include $(CFLAGS) test_runner.c ../src/print_jobs.c ../../../sim/lvgl/src/extra/libs/qrcode/qrcodegen.c -o $@

$(BUILD)/transport_test: transport_test.c ../src/printer_transport.c ../include/printer_transport.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(SIMFLAGS) -std=gnu99 -g -Wall -Wextra -Werror -Wno-error=comment \
	    transport_test.c ../src/printer_transport.c -o $@

$(BUILD)/qr_request_test: qr_request_test.c ../src/qr_request.c ../src/https_client.c ../include/qr_request.h ../include/https_client.h ../config/conf.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(SIMFLAGS) -DSIM_STANDALONE_TEST -include ../config/conf.h \
	    $(filter-out -std=c99 -pedantic,$(CFLAGS)) -Wno-error=comment -std=gnu99 qr_request_test.c \
	    ../src/qr_request.c ../src/https_client.c ../../../sim/include/cJSON.c -o $@

$(BUILD)/mqtt_test: mqtt_test.c ../src/mqtt_wire.c ../include/mqtt_wire.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) mqtt_test.c ../src/mqtt_wire.c -o $@

$(BUILD)/mqtt_message_test: mqtt_message_test.c ../src/mqtt_listener.c ../include/mqtt_listener.h ../src/mqtt_wire.c ../include/mqtt_wire.h ../src/soundbox_event.c ../include/soundbox_event.h ../include/soundbox_store.h ../config/conf.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(SIMFLAGS) -include ../config/conf.h -DNETZME_NATIVE_MQTT_ABI -DSIM_MQTT_TEST \
	    -std=gnu99 -g -Wall -Wextra -Werror -Wno-error=comment -ffunction-sections \
	    mqtt_message_test.c ../src/mqtt_wire.c ../src/soundbox_event.c ../../../sim/include/cJSON.c -o $@ -Wl,--gc-sections -Wl,--wrap=connect -lpthread

SIM_OBJECTS = $(filter-out $(BUILD)/../stubs_sim_main.c.o \
	%src_mqtt_listener.c.o \
	%logger_logger.c.o %logger_logger_uart.c.o %logger_logger_file.c.o, \
	$(wildcard $(BUILD)/../*.o))

.PHONY: sim-objects
sim-objects:
	$(MAKE) -C ../../../sim APP=PrinterDemo -j4 > /tmp/printerdemo-ui-build.log 2>&1 || { tail -c 4000 /tmp/printerdemo-ui-build.log; exit 1; }

SOUNDBOX_SOURCES := ../src/soundbox_ui.c ../src/soundbox_app.c ../src/soundbox_store.c ../src/soundbox_event.c ../src/soundbox_backend.c ../src/qris_tap.c ../src/https_client.c $(wildcard ../src/fonts/*.c)
SOUNDBOX_HEADERS := ../include/soundbox_ui.h ../include/soundbox_images.h ../include/soundbox_fonts.h ../include/soundbox_app.h ../include/soundbox_store.h ../include/soundbox_event.h ../include/soundbox_backend.h ../include/qris_tap.h ../include/print_jobs.h ../include/printer_transport.h $(wildcard ../src/fonts/*.[ch])

$(BUILD)/ui_test: ui_test.c ../src/main.c ../src/print_jobs.c ../src/printer_transport.c ../config/conf.h ../include/qr_request.h ../include/https_client.h ../include/print_jobs.h ../include/printer_transport.h $(SOUNDBOX_SOURCES) $(SOUNDBOX_HEADERS) | sim-objects
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(SIMFLAGS) -I../config -include ../config/conf.h \
	    -std=gnu99 -g -Wall -Wextra -Werror -Wno-error=comment ui_test.c ../src/print_jobs.c ../src/printer_transport.c $(SOUNDBOX_SOURCES) \
	    $(SIM_OBJECTS) -o $@ $$(sdl2-config --libs) -lpthread -lm -lssl -lcrypto -Wl,-T,../../../sim/sim_sections.ld

link-fsroot:
	ln -sfn ../../../sim/fsroot fsroot

check-menus: link-fsroot $(BUILD)/soundbox_menu_four_test $(BUILD)/soundbox_menu_manual_test
	$(BUILD)/soundbox_menu_four_test
	$(BUILD)/soundbox_menu_manual_test

$(BUILD)/soundbox_menu_four_test: CPPFLAGS += -DSOUNDBOX_MENU_MOTOR=1 -DSOUNDBOX_MENU_MOBIL=1 -DSOUNDBOX_MENU_SEPEDA=1 -DSOUNDBOX_MENU_ELF=0 -DSOUNDBOX_MENU_BUS=1 -DSOUNDBOX_MENU_MANUAL=0
$(BUILD)/soundbox_menu_manual_test: CPPFLAGS += -DSOUNDBOX_MENU_MOTOR=0 -DSOUNDBOX_MENU_MOBIL=0 -DSOUNDBOX_MENU_SEPEDA=0 -DSOUNDBOX_MENU_ELF=0 -DSOUNDBOX_MENU_BUS=0 -DSOUNDBOX_MENU_MANUAL=1
$(BUILD)/soundbox_full_test: CPPFLAGS += -DSOUNDBOX_MENU_MOTOR=1 -DSOUNDBOX_MENU_MOBIL=1 -DSOUNDBOX_MENU_SEPEDA=1 -DSOUNDBOX_MENU_ELF=1 -DSOUNDBOX_MENU_BUS=1 -DSOUNDBOX_MENU_MANUAL=1
$(BUILD)/soundbox_test $(BUILD)/soundbox_full_test $(BUILD)/soundbox_menu_four_test $(BUILD)/soundbox_menu_manual_test: CPPFLAGS += -DSOUNDBOX_CPM_LIVE_ENABLED=1 -DSOUNDBOX_TAP_LIVE_ENABLED=1

$(BUILD)/soundbox_test $(BUILD)/soundbox_full_test $(BUILD)/soundbox_menu_four_test $(BUILD)/soundbox_menu_manual_test: soundbox_test.c ../src/main.c $(SOUNDBOX_SOURCES) $(SOUNDBOX_HEADERS) ../src/print_jobs.c ../src/printer_transport.c ../config/conf.h | sim-objects
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(SIMFLAGS) -I../config -include ../config/conf.h \
	    -std=gnu99 -g -Wall -Wextra -Werror -Wno-error=comment soundbox_test.c ../src/print_jobs.c ../src/printer_transport.c $(SOUNDBOX_SOURCES) \
	    $(SIM_OBJECTS) -o $@ $$(sdl2-config --libs) -lpthread -lm -lssl -lcrypto -Wl,-T,../../../sim/sim_sections.ld -Wl,--wrap=lv_tick_get -Wl,--wrap=soundbox_backend_execute -Wl,--wrap=printer_send

$(BUILD)/store_test: store_test.c ../src/soundbox_store.c ../src/soundbox_event.c ../include/soundbox_store.h ../include/soundbox_event.h ../config/conf.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(SIMFLAGS) -include ../config/conf.h -std=gnu99 -g -Wall -Wextra -Werror -Wno-error=comment \
	    store_test.c ../src/soundbox_store.c ../src/soundbox_event.c ../../../sim/include/cJSON.c -o $@