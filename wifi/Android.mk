# Copyright 2006 The Android Open Source Project

ifdef WIFI_DRIVER_MODULE_PATH
LOCAL_CFLAGS += -DWIFI_DRIVER_MODULE_PATH=\"$(WIFI_DRIVER_MODULE_PATH)\"
endif
ifdef WIFI_DRIVER_MODULE_ARG
LOCAL_CFLAGS += -DWIFI_DRIVER_MODULE_ARG=\"$(WIFI_DRIVER_MODULE_ARG)\"
endif
ifdef WIFI_DRIVER_P2P_MODULE_ARG
LOCAL_CFLAGS += -DWIFI_DRIVER_P2P_MODULE_ARG=\"$(WIFI_DRIVER_P2P_MODULE_ARG)\"
endif
ifdef WIFI_DRIVER_MODULE_NAME
LOCAL_CFLAGS += -DWIFI_DRIVER_MODULE_NAME=\"$(WIFI_DRIVER_MODULE_NAME)\"
endif

ifdef WIFI_SDIO_IF_DRIVER_MODULE_PATH
LOCAL_CFLAGS += -DWIFI_SDIO_IF_DRIVER_MODULE_PATH=\"$(WIFI_SDIO_IF_DRIVER_MODULE_PATH)\"
endif
ifdef WIFI_SDIO_IF_DRIVER_MODULE_ARG
LOCAL_CFLAGS += -DWIFI_SDIO_IF_DRIVER_MODULE_ARG=\"$(WIFI_SDIO_IF_DRIVER_MODULE_ARG)\"
endif
ifdef WIFI_SDIO_IF_DRIVER_MODULE_NAME
LOCAL_CFLAGS += -DWIFI_SDIO_IF_DRIVER_MODULE_NAME=\"$(WIFI_SDIO_IF_DRIVER_MODULE_NAME)\"
endif

ifdef WIFI_COMPAT_MODULE_PATH
LOCAL_CFLAGS += -DWIFI_COMPAT_MODULE_PATH=\"$(WIFI_COMPAT_MODULE_PATH)\"
endif
ifdef WIFI_COMPAT_MODULE_ARG
LOCAL_CFLAGS += -DWIFI_COMPAT_MODULE_ARG=\"$(WIFI_COMPAT_MODULE_ARG)\"
endif
ifdef WIFI_COMPAT_MODULE_NAME
LOCAL_CFLAGS += -DWIFI_COMPAT_MODULE_NAME=\"$(WIFI_COMPAT_MODULE_NAME)\"
endif

ifdef WIFI_FIRMWARE_LOADER
LOCAL_CFLAGS += -DWIFI_FIRMWARE_LOADER=\"$(WIFI_FIRMWARE_LOADER)\"
endif
ifdef WIFI_DRIVER_LOADER_DELAY
LOCAL_CFLAGS += -DWIFI_DRIVER_LOADER_DELAY=$(WIFI_DRIVER_LOADER_DELAY)
endif
ifdef WIFI_DRIVER_FW_PATH_STA
LOCAL_CFLAGS += -DWIFI_DRIVER_FW_PATH_STA=\"$(WIFI_DRIVER_FW_PATH_STA)\"
endif
ifdef WIFI_DRIVER_FW_PATH_AP
LOCAL_CFLAGS += -DWIFI_DRIVER_FW_PATH_AP=\"$(WIFI_DRIVER_FW_PATH_AP)\"
endif
ifdef WIFI_DRIVER_FW_PATH_P2P
LOCAL_CFLAGS += -DWIFI_DRIVER_FW_PATH_P2P=\"$(WIFI_DRIVER_FW_PATH_P2P)\"
endif
ifdef WIFI_DRIVER_FW_PATH_PARAM
LOCAL_CFLAGS += -DWIFI_DRIVER_FW_PATH_PARAM=\"$(WIFI_DRIVER_FW_PATH_PARAM)\"
endif

ifdef WIFI_BYPASS_FWRELOAD
LOCAL_CFLAGS += -DWIFI_BYPASS_FWRELOAD=\"$(WIFI_BYPASS_FWRELOAD)\"
endif

ifeq ($(BOARD_WLAN_DEVICE),UNITE)
  LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../external/wpa_supplicant_ath/wpa_supplicant/src/common
  LOCAL_SRC_FILES += wifi/wifi_unite.c
else ifeq ($(BOARD_WLAN_VENDOR), INTEL)
  LOCAL_SRC_FILES += wifi/wifi_intel.c
  LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../external/wpa_supplicant_8/src/common
else
  LOCAL_SRC_FILES += wifi/wifi.c
  LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../external/wpa_supplicant_8/src/common
endif

ifneq ($(WPA_SUPPLICANT_VERSION), none)
LOCAL_CFLAGS += -DLIBWPA_CLIENT_EXISTS
LOCAL_SHARED_LIBRARIES += libwpa_client
endif
LOCAL_SHARED_LIBRARIES += libnetutils
