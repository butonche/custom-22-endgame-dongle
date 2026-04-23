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
