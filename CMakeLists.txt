# Stripped efogdev input processors + custom scroll scaler.
# Mixer, accel curves, scroll scaler, rate limiter run on peripheral.

zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/include)

if(CONFIG_ZMK_POINTER_2S_MIXER)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/pointer_2s_mixer.c)
endif()

if(CONFIG_ZMK_ACCEL_CURVE)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/accel_curve.c)
endif()

if(CONFIG_ZMK_INPUT_PROCESSOR_REPORT_RATE_LIMIT)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/report_rate_limit.c)
endif()

if(CONFIG_ZMK_SCROLL_SCALER)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/scroll_scaler.c)
endif()

if(CONFIG_ZMK_6DOF OR CONFIG_ZMK_6DOF_RELAY OR CONFIG_ZMK_INPUT_PROCESSOR_6DOF)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/sixdof_mode.c)
endif()

if(CONFIG_ZMK_INPUT_MOCK_SENSOR)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/sixdof_layer_listener.c)
endif()

if(CONFIG_ZMK_6DOF_RELAY)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/sixdof_relay.c)
endif()

if(CONFIG_ZMK_INPUT_PROCESSOR_6DOF)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/input_processor_6dof.c)
endif()

if(CONFIG_ZMK_INPUT_MOCK_SENSOR)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/input_mock_sensor.c)
endif()

if(CONFIG_ZMK_HID_JOYSTICK)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/hid-joystick/endpoints.c)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/hid-joystick/hid_joystick.c)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/hid-joystick/usb_hid.c)
endif()
