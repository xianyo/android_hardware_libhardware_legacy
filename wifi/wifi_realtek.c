/*
 * Copyright 2008, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "hardware_legacy/wifi.h"
#include "libwpa_client/wpa_ctrl.h"

#define LOG_TAG "WifiHW"
#define LOG_NDEBUG 0
#include "cutils/log.h"
#include "cutils/memory.h"
#include "cutils/misc.h"
#include "cutils/properties.h"
#include "private/android_filesystem_config.h"
#ifdef HAVE_LIBC_SYSTEM_PROPERTIES
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>
#endif

static struct wpa_ctrl *ctrl_conn;
static struct wpa_ctrl *monitor_conn;

extern int do_dhcp();
extern int ifc_init();
extern void ifc_close();
extern int ifc_up(const char *name);
extern char *dhcp_lasterror();
extern void get_dhcp_info();
extern int init_module(void *, unsigned long, const char *);
extern int delete_module(const char *, unsigned int);

static char g_ifname[PROPERTY_VALUE_MAX];
// TODO: use new ANDROID_SOCKET mechanism, once support for multiple
// sockets is in


//#define CONFIG_WIFI_BUILT_IN_KERNEL

#define WIFI_DRIVER_IFNAME			"wlan%d"
#define WIFI_DRIVER_IFNAME_AP		"wlap%d"
#define WIFI_DRIVER_IFNAME_DISABLE	"disable%d"

#ifndef WIFI_DRIVER_MODULE_PATH
#define WIFI_DRIVER_MODULE_PATH	"/system/wifi/wlan.ko"
#endif
#ifndef WIFI_DRIVER_MODULE_NAME
#define WIFI_DRIVER_MODULE_NAME	 "wlan"
#endif
#ifndef WIFI_DRIVER_MODULE_ARG
#define WIFI_DRIVER_MODULE_ARG	""
#endif

#ifndef WIFI_DRIVER_MODULE_PATH_AP
#define WIFI_DRIVER_MODULE_PATH_AP	WIFI_DRIVER_MODULE_PATH
#endif
#ifndef WIFI_DRIVER_MODULE_ARG_AP
#define WIFI_DRIVER_MODULE_ARG_AP	"ifname=" WIFI_DRIVER_IFNAME_AP
#endif

#ifdef USE_DRIVER_PROP_PATH_NAME
static const char DRIVER_PROP_PATH_NAME[]	= "wlan.driver.path";
#endif
#ifdef USE_DRIVER_PROP_IF_NAME
static const char DRIVER_PROP_IF_NAME[]		= "wlan.driver.ifname";
#endif
static const char DRIVER_PROP_NAME[]			= "wlan.driver.status";

static const char WIFI_DO_RMMOD_PROP[]		="wlan.driver.do.rmmod";


static const char DRIVER_MODULE_NAME[]	= WIFI_DRIVER_MODULE_NAME;
static const char DRIVER_MODULE_TAG[]		= WIFI_DRIVER_MODULE_NAME " ";
static const char DRIVER_MODULE_PATH[]	= WIFI_DRIVER_MODULE_PATH;
static const char DRIVER_MODULE_ARG[]		= WIFI_DRIVER_MODULE_ARG;

static const char DRIVER_MODULE_PATH_AP[]	= WIFI_DRIVER_MODULE_PATH_AP;
static const char DRIVER_MODULE_ARG_AP[]		= WIFI_DRIVER_MODULE_ARG_AP;

static const char WPA_SUPPLICANT_NAME[]			= "wpa_supplicant";
static const char WPA_SUPP_PROP_NAME[]			= "init.svc.wpa_supplicant";
static const char WPA_SUPP_CTRL_DIR[]				= "/data/misc/wifi/sockets";
static const char WPA_SUPP_CONFIG_TEMPLATE[]	= "/system/etc/wifi/wpa_supplicant.conf";
static const char WPA_SUPP_CONFIG_FILE[]			= "/data/misc/wifi/wpa_supplicant.conf";

static const char HOSTAPD_NAME[]				= "hostapd";
static const char HOSTAPD_PROP_NAME[]		= "init.svc.hostapd";
static const char HOSTAPD_CTRL_DIR[]			= "/data/misc/wifi/hostapd";
static const char HOSTAPD_CONFIG_TEMPLATE[]	= "/system/etc/wifi/hostapd.conf";
static const char HOSTAPD_CONFIG_FILE[]    		= "/data/misc/wifi/hostapd.conf";

static const char MODULE_FILE[]	= "/proc/modules";

#if 0
static const char WAPI_SUPPLICANT_NAME[] = "wapic";
static const char WAPI_SUPP_PROP_NAME[] ="init.svc.wapic";
const char* pSupplicantName = WPA_SUPPLICANT_NAME;//WAPI_SUPPLICANT_NAME;
const char* pSupplicantPropName = WPA_SUPP_PROP_NAME;//WAPI_SUPP_PROP_NAME;
#endif


#include <linux/wireless.h>

int get_priv_func_num(int sockfd, const char *ifname, const char *fname)
{
	struct iwreq wrq;
	struct iw_priv_args *priv_ptr;
	int i, ret;
	char *buf;

	if( NULL == (buf=(char *)malloc(4096)) ) {
		ret = -ENOMEM;
		goto exit;
	}

	strncpy(wrq.ifr_name, ifname, sizeof(wrq.ifr_name));
	wrq.u.data.pointer = buf;
	wrq.u.data.length = 4096 / sizeof(struct iw_priv_args);
	wrq.u.data.flags = 0;
	if ((ret = ioctl(sockfd, SIOCGIWPRIV, &wrq)) < 0) {
		LOGE("SIOCGIPRIV failed: %d %s", ret, strerror(errno));
		goto exit;
	}

	ret = -EOPNOTSUPP;
	priv_ptr = (struct iw_priv_args *)wrq.u.data.pointer;
	for(i=0;(i < wrq.u.data.length);i++) {
		if (strcmp(priv_ptr[i].name, fname) == 0) {
			ret = priv_ptr[i].cmd;
			break;
		}
	}

exit:
	if(buf)
		free(buf);

	return ret;
}

int rtl871x_drv_rereg_nd_name_fd(int sockfd, const char *ifname, const char * new_ifname)
{
	struct iwreq wrq;
	int ret;
	
	int fnum;
	char *fname = "rereg_nd_name";
	
	char ifname_buf[IFNAMSIZ];

	ret = fnum = get_priv_func_num(sockfd, ifname, fname);
	if(ret < 0) {
		LOGE("get_priv_func_num(%s) return %d", fname, ret);
		goto exit;
	}
	
	strncpy(wrq.ifr_name, ifname, sizeof(wrq.ifr_name));
	
	strncpy(ifname_buf, new_ifname, IFNAMSIZ);
	ifname_buf[IFNAMSIZ-1] = 0;
	wrq.u.data.pointer = ifname_buf;
	wrq.u.data.length = strlen(ifname_buf)+1;
	wrq.u.data.flags = 0;

	ret = ioctl(sockfd, fnum, &wrq);
	if (ret) {
		LOGE("ioctl - failed: %d %s", ret, strerror(errno));
	}
exit:
	return ret;
}

int rtl871x_drv_rereg_nd_name(const char *ifname, const char *new_ifname)
{
	int sockfd;
	int ret;

#if 0
	if (ifc_init() < 0)
		return -1;	
	if (ifc_up(ifname)) {
		LOGD("failed to bring up interface %s: %s\n", ifname, strerror(errno));
		return -1;
	}
#endif
	
	sockfd = socket(PF_INET, SOCK_DGRAM, 0);
	if (sockfd< 0) {
		perror("socket[PF_INET,SOCK_DGRAM]");
		ret = -1;
		goto bad;
	}

	ret = rtl871x_drv_rereg_nd_name_fd(
		sockfd
		, ifname
		, new_ifname
	);
	
	close(sockfd);
bad:
	return ret;
}

int rtl871x_drv_set_pid_fd(int sockfd, const char *ifname, const int index, const int pid)
{
	struct iwreq wrq;
	int ret;
	int fnum;
	char *fname = "setpid";

	int req[2];

	ret = fnum = get_priv_func_num(sockfd, ifname, fname);
	if(ret < 0) {
		LOGE("get_priv_func_num(%s) return %d", fname, ret);
		goto exit;
	}

	strncpy(wrq.ifr_name, ifname, sizeof(wrq.ifr_name));
	
	req[0]=index;
	req[1]=pid;
	memcpy(wrq.u.name,req,sizeof(int)*2);

	ret = ioctl(sockfd, fnum, &wrq);
	if (ret) {
		LOGE("ioctl - failed: %d %s", ret, strerror(errno));
	}
exit:
	return ret;
}

int rtl871x_drv_set_pid(const char *ifname, const int index, const int pid)
{
	int sockfd;
	int ret;

#if 0
	if (ifc_init() < 0)
		return -1;	
	if (ifc_up(ifname)) {
		LOGD("failed to bring up interface %s: %s\n", ifname, strerror(errno));
		return -1;
	}
#endif
	
	sockfd = socket(PF_INET, SOCK_DGRAM, 0);
	if (sockfd< 0) {
		perror("socket[PF_INET,SOCK_DGRAM]");
		ret = -1;
		goto bad;
	}

	ret = rtl871x_drv_set_pid_fd(
		sockfd
		, ifname
		, index
		, pid
	);

	close(sockfd);
bad:
	return ret;
}


static int insmod(const char *filename, const char *params)
{
	void *module;
	unsigned int size;
	int ret;

	module = load_file(filename, &size);
	if (!module) {
		LOGE(" load module:%s failed", filename);
		return -1;
	}

	ret = init_module(module, size, params);
	free(module);
	return ret;
}

static int rmmod(const char *modname)
{
	int ret = -1;
	int maxtry = 10;

	while (maxtry-- > 0) {
		ret = delete_module(modname, O_NONBLOCK | O_EXCL);
		if (ret < 0 && errno == EAGAIN)
			usleep(500000);
		else
			break;
	}

	if (ret != 0)
		LOGD("Unable to unload driver module \"%s\": %s\n", modname, strerror(errno));
	if(errno == ENOENT)
		ret=0;
	return ret;
}

#if 0
int getWifiIfname(char *ifname)
{
	char linebuf[1024];
	FILE *f = fopen("/proc/net/wireless", "r");
	
	*ifname = '\0';
	if (f) {
		while(fgets(linebuf, sizeof(linebuf)-1, f)) {
			
			if (strchr(linebuf, ':')) {
				char *dest = ifname;
				char *p = linebuf;
				
				while(*p && isspace(*p))
					++p;
				while (*p && *p != ':') {
					*dest++ = *p++;
				}
				*dest = '\0';
				LOGD("getWifiIfname: %s\n", ifname);
				fclose(f);
				return 0;
			}
		}
		fclose(f);
	} 
	return -1;
}
#else
int DetectWifiIfNameFromProc()
{
	char linebuf[1024];
	FILE *f = fopen("/proc/net/wireless", "r");
	
	g_ifname[0] = '\0';
	if (f) {
		while(fgets(linebuf, sizeof(linebuf)-1, f)) {
			
			if (strchr(linebuf, ':')) {
				char *dest = g_ifname;
				char *p = linebuf;
				
				while(*p && isspace(*p))
					++p;
				while (*p && *p != ':') {
					*dest++ = *p++;
				}
				*dest = '\0';
				LOGD("DetectWifiIfNameFromProc: %s\n", g_ifname);
				fclose(f);
				return 0;
			}
		}
		fclose(f);
	} 
	return -1;
}

char *getWifiIfname()
{
	DetectWifiIfNameFromProc();
	return g_ifname;
}
#endif


#if 0
#define NDD_DEV_MAJOR           240
//#define IOCTL_NDD_GET_SERIAL	_IO(NDD_DEV_MAJOR, 7)
#define IOCTL_NDD_GET_SERIALNUMBER  _IO(NDD_DEV_MAJOR, 8)
#define MAC_ADDRESS_SIZE		17+1
#define SERIAL_NUMBER_SIZE		32
#endif

static int load_driver(const char *module_path, const char *module_name, const char *module_arg)
{
	pid_t pid;
	//char drvFullPath[256];
	//char drvFullPathScript[256];
	//char drvScript[PROPERTY_VALUE_MAX];
	char drvPath[PROPERTY_VALUE_MAX];
	char defIfname[256];
	char local_ifname[PROPERTY_VALUE_MAX];

	#ifdef USE_DRIVER_PROP_PATH_NAME
	if ( !property_get(DRIVER_PROP_PATH_NAME, drvPath, module_path)) {
		LOGD("Cannot get driver path property\n");
		goto err_exit;
	}
	#else
	snprintf(drvPath, sizeof(drvPath)-1, "%s", module_path);
	#endif
	
	LOGD("driver path %s",drvPath );

	if (insmod(drvPath, module_arg)!=0) {
		LOGD("Fail to insmod driver\n");
		goto err_exit;
	} 


	#if 0
	#ifdef USE_DRIVER_PROP_IF_NAME
	getWifiIfname(defIfname);
	property_get(DRIVER_PROP_IF_NAME, ifname, defIfname);
	#else
	getWifiIfname(ifname);
	#endif
	#endif

	if(*getWifiIfname() == '\0') {
		LOGD("load_driver: getWifiIfname fail");
		goto err_rmmod;
	}

	strncpy(local_ifname,getWifiIfname(),PROPERTY_VALUE_MAX);
	local_ifname[PROPERTY_VALUE_MAX-1]=0;
	
	
	LOGD("insmod %s done, ifname:%s",drvPath, local_ifname);

	pid = vfork();
	if (pid!=0) {
		//wait the child to do ifup and check the result...
		
		int ret;
		int status;
		int cnt = 10;

		while ( (ret=waitpid(pid, &status, WNOHANG)) == 0 && cnt-- > 0 ) {
			LOGD("still waiting...\n");
			sleep(1);
		}

		LOGD("waitpid finished ret %d\n", ret);
		if (ret>0) {
			if (WIFEXITED(status)) {
				LOGD("child process exited normally, with exit code %d\n", WEXITSTATUS(status));
			} else {
				LOGD("child process exited abnormally\n");
				goto err_rmmod;
			}
			return 0;
		}
		goto err_rmmod;
		

	} else {       
		//do ifup here, and let parent to monitor the result...
		if (strcmp(local_ifname, "sta") == 0)
			_exit(0);

		if (ifc_init() < 0)
			_exit(-1);
		
		if (ifc_up(local_ifname)) {
			LOGD("failed to bring up interface %s: %s\n", local_ifname, strerror(errno));
			_exit(-1);
		}
		ifc_close();
		_exit(0);
	}

err_rmmod:
	rmmod(module_name);
err_exit:	
	return -1;
}

int do_dhcp_request(int *ipaddr, int *gateway, int *mask,
                    int *dns1, int *dns2, int *server, int *lease) {
    /* For test driver, always report success */
    if (strcmp(g_ifname, "sta") == 0)
        return 0;

    if (ifc_init() < 0)
        return -1;

    if (do_dhcp(g_ifname) < 0) {
        ifc_close();
        return -1;
    }
    ifc_close();
    get_dhcp_info(ipaddr, gateway, mask, dns1, dns2, server, lease);
    return 0;
}

const char *get_dhcp_error_string() {
    return dhcp_lasterror();
}

static int check_driver_loaded(const char *drv_module_tag, const char *drv_prop_name) {
	char driver_status[PROPERTY_VALUE_MAX];
	FILE *proc;
	char line[sizeof(drv_module_tag)+10];

	if (!property_get(drv_prop_name, driver_status, NULL)
		|| strcmp(driver_status, "ok") != 0) {
		return 0;  /* driver not loaded */
	}
	/*
	 * If the property says the driver is loaded, check to
	 * make sure that the property setting isn't just left
	 * over from a previous manual shutdown or a runtime
	 * crash.
	 */
	if ((proc = fopen(MODULE_FILE, "r")) == NULL) {
		LOGW("Could not open %s: %s", MODULE_FILE, strerror(errno));
		property_set(drv_prop_name, "unloaded");	
		return 0;
	}
	while ((fgets(line, sizeof(line), proc)) != NULL) {
		if (strncmp(line, drv_module_tag, strlen(drv_module_tag)) == 0) {
			fclose(proc);
			return 1;
		}
	}
	fclose(proc);
	property_set(drv_prop_name, "unloaded");
	return 0;
}

int ensure_config_file_exists(const char *conf_file, const char *tmplate)
{
    char buf[2048];
    int srcfd, destfd;
    int nread;

    if (access(conf_file, R_OK|W_OK) == 0) {
        return 0;
    } else if (errno != ENOENT) {
        LOGE("Cannot access \"%s\": %s", conf_file, strerror(errno));
        return -1;
    }

    srcfd = open(tmplate, O_RDONLY);
    if (srcfd < 0) {
        LOGE("Cannot open \"%s\": %s", tmplate, strerror(errno));
        return -1;
    }

    destfd = open(conf_file, O_CREAT|O_WRONLY, 0660);
    if (destfd < 0) {
        close(srcfd);
        LOGE("Cannot create \"%s\": %s", conf_file, strerror(errno));
        return -1;
    }

    while ((nread = read(srcfd, buf, sizeof(buf))) != 0) {
        if (nread < 0) {
            LOGE("Error reading \"%s\": %s", tmplate, strerror(errno));
            close(srcfd);
            close(destfd);
            unlink(conf_file);
            return -1;
        }
        write(destfd, buf, nread);
    }

    close(destfd);
    close(srcfd);

    if (chown(conf_file, AID_SYSTEM, AID_WIFI) < 0) {
        LOGE("Error changing group ownership of %s to %d: %s",
             conf_file, AID_WIFI, strerror(errno));
        unlink(conf_file);
        return -1;
    }
    return 0;
}


/*********************************************
The following 

*********************************************/

int wifi_load_ap_driver()
{
    char driver_status[PROPERTY_VALUE_MAX];
    int ret;

#if !defined(CONFIG_WIFI_BUILT_IN_KERNEL)


    if (check_driver_loaded(DRIVER_MODULE_TAG, DRIVER_PROP_NAME)) {

	if(*getWifiIfname() == '\0') {
		LOGD("wifi_load_ap_driver: getWifiIfname fail");
		return -1;
	}
	
	rtl871x_drv_rereg_nd_name(getWifiIfname(), WIFI_DRIVER_IFNAME_AP);
        return 0;
    }
    if ( (ret=load_driver(DRIVER_MODULE_PATH_AP, DRIVER_MODULE_NAME, DRIVER_MODULE_ARG_AP))==0 ) {
        property_set(DRIVER_PROP_NAME, "ok");
        LOGI("wifi_load_ap_driver: return 0\n");
        return 0;
    }

    property_set(DRIVER_PROP_NAME, "timeout");

    LOGI("wifi_load_ap_driver: return -1\n");
    return -1;
#else

	// Could do wifi power switch here, ex:
	// 1. insmod specific .ko to do power switch or
	// 2. open and write to power switch file...
	//

	// After power on, we should got wifi interface here
	if(*getWifiIfname() == '\0') {
		LOGD("wifi_load_ap_driver: getWifiIfname fail");
		return -1;
	}

	rtl871x_drv_rereg_nd_name(getWifiIfname(), WIFI_DRIVER_IFNAME_AP);
	return 0;
#endif
}

int wifi_load_driver()
{
    char driver_status[PROPERTY_VALUE_MAX];
    int ret;

#if !defined(CONFIG_WIFI_BUILT_IN_KERNEL)
    if (check_driver_loaded(DRIVER_MODULE_TAG, DRIVER_PROP_NAME)) {
	if(*getWifiIfname() == '\0') {
		LOGD("wifi_load_driver: getWifiIfname fail");
		return -1;
	}
	rtl871x_drv_rereg_nd_name(getWifiIfname(), WIFI_DRIVER_IFNAME);
        return 0;
    }
    if ( (ret=load_driver(DRIVER_MODULE_PATH, DRIVER_MODULE_NAME, DRIVER_MODULE_ARG))==0 ) {
        property_set(DRIVER_PROP_NAME, "ok");
        LOGI("wifi_load_driver: return 0\n");
        return 0;
    }

    property_set(DRIVER_PROP_NAME, "timeout");

    LOGI("wifi_load_driver: return -1\n");
    return -1;
#else
	// Could do wifi power switch here, ex:
	// 1. insmod specific .ko to do power switch or
	// 2. open and write to power switch file...
	//

	// After power on, we should got wifi interface here
	if(*getWifiIfname() == '\0') {
		LOGD("wifi_load_driver: getWifiIfname fail");
		return -1;
	}
	rtl871x_drv_rereg_nd_name(getWifiIfname(), WIFI_DRIVER_IFNAME);
	return 0;
#endif
}

int wifi_unload_driver()
{
    int count = 20; /* wait at most 10 seconds for completion */
    int ret;
	char wifi_do_rmmod[PROPERTY_VALUE_MAX];

#if !defined(CONFIG_WIFI_BUILT_IN_KERNEL) 

	if (!property_get(WIFI_DO_RMMOD_PROP, wifi_do_rmmod, "yes") || strcmp(wifi_do_rmmod, "yes") != 0) {
		if (check_driver_loaded(DRIVER_MODULE_TAG, DRIVER_PROP_NAME)){
			if(*getWifiIfname() != '\0')
				rtl871x_drv_rereg_nd_name(getWifiIfname(), WIFI_DRIVER_IFNAME_DISABLE);
		}
		return 0;
	}

    if ( (ret=rmmod(DRIVER_MODULE_NAME)) == 0) {        
        while (count-- > 0) {
            if (!check_driver_loaded(DRIVER_MODULE_TAG, DRIVER_PROP_NAME))
                break;
    	    usleep(500000);
        }
        if (count) {
            return 0;
        }
    }
    LOGE("wifi_unload_driver: fail to unload driver %d\n", ret);
    return -1;
#else
	// Could do wifi power switch here, ex:
	// 1. insmod specific .ko to do power switch or
	// 2. open and write to power switch file...
	//

	// After power off, we could mask off the followings...
	if(*getWifiIfname() == '\0') {
		LOGD("wifi_unload_driver: getWifiIfname fail");
		return -1;
	}
	rtl871x_drv_rereg_nd_name(getWifiIfname(), WIFI_DRIVER_IFNAME_DISABLE);
	return 0;
#endif
}

int wifi_start_supplicant()
{
    char supp_status[PROPERTY_VALUE_MAX] = {'\0'};
    int count = 200; /* wait at most 20 seconds for completion */
    LOGI("wifi_start_supplicant enter");
#ifdef HAVE_LIBC_SYSTEM_PROPERTIES
    const prop_info *pi;
    unsigned serial = 0;
#endif

    /* Check whether already running */
    if (property_get(WPA_SUPP_PROP_NAME, supp_status, NULL)
            && strcmp(supp_status, "running") == 0) {
        LOGI("wifi_start_supplicant alredy run. leave");
        return 0;
    }

    /* Before starting the daemon, make sure its config file exists */
    if (ensure_config_file_exists(WPA_SUPP_CONFIG_FILE, WPA_SUPP_CONFIG_TEMPLATE) < 0) {
        LOGE("Wi-Fi will not be enabled");
        return -1;
    }

    /* Clear out any stale socket files that might be left over. */
    wpa_ctrl_cleanup();

#ifdef HAVE_LIBC_SYSTEM_PROPERTIES
    /*
     * Get a reference to the status property, so we can distinguish
     * the case where it goes stopped => running => stopped (i.e.,
     * it start up, but fails right away) from the case in which
     * it starts in the stopped state and never manages to start
     * running at all.
     */
    pi = __system_property_find(WPA_SUPP_PROP_NAME);
    if (pi != NULL) {
        serial = pi->serial;
    }
#endif
    LOGI("wifi_start_supplicant before ctl.start pi %p, serial %u", pi, serial);
    property_set("ctl.start", WPA_SUPPLICANT_NAME);
    sched_yield();

    while (count-- > 0) {
 #ifdef HAVE_LIBC_SYSTEM_PROPERTIES
        if (pi == NULL) {
            pi = __system_property_find(WPA_SUPP_PROP_NAME);
        }
        if (pi != NULL) {
            __system_property_read(pi, NULL, supp_status);
            if (strcmp(supp_status, "running") == 0) {
                LOGI("wifi_start_supplicant is running now. leave");
                return 0;
            } else if (pi->serial != serial &&
                       strcmp(supp_status, "stopped") == 0) {
                LOGI("wifi_start_supplicant stopped pi->serial %u serial %u leave", pi->serial, serial);
                if (serial==0) { /* no initialized, skip it */
                    serial = pi->serial;
                } else {
                    return -1;
                }
            }
        }
#else
        if (property_get(WPA_SUPP_PROP_NAME, supp_status, NULL)) {
            if (strcmp(supp_status, "running") == 0) {
                LOGI("wifi_start_supplicant is running now. leave");
                return 0;
            }
        }
#endif
        usleep(100000);
    }
    LOGI("wifi_start_supplicant is NOT running. timeout. leave");
    return -1;
}

int wifi_stop_supplicant()
{
    char supp_status[PROPERTY_VALUE_MAX] = {'\0'};
    int count = 50; /* wait at most 5 seconds for completion */

    /* Check whether supplicant already stopped */
    if (property_get(WPA_SUPP_PROP_NAME, supp_status, NULL)
        && strcmp(supp_status, "stopped") == 0) {
        return 0;
    }

    property_set("ctl.stop", WPA_SUPPLICANT_NAME);
    sched_yield();

    while (count-- > 0) {
        if (property_get(WPA_SUPP_PROP_NAME, supp_status, NULL)) {
            if (strcmp(supp_status, "stopped") == 0)
                return 0;
        }
        usleep(100000);
    }
    return -1;
}


#if 0
char* wifi_get_supplicant_name()
{
	LOGW("------ %s ----\n", pSupplicantName);
	return pSupplicantName;
}

int wifi_change_supplicant_name() {
	if(pSupplicantName == WPA_SUPPLICANT_NAME) {
		pSupplicantName = WAPI_SUPPLICANT_NAME;
		pSupplicantPropName = WAPI_SUPP_PROP_NAME;
	}
	else if ( pSupplicantName == WAPI_SUPPLICANT_NAME ) {
		pSupplicantName = WPA_SUPPLICANT_NAME;
		pSupplicantPropName = WPA_SUPP_PROP_NAME;
	}
	else
		return 0;
	return 1;
}
#endif

int wifi_connect_to_supplicant()
{
	char defIfname[256];
	char ctrl_conn_path[256];
	char supp_status[PROPERTY_VALUE_MAX] = {'\0'};
	int retry_times = 20;

	LOGI("wifi_connect_to_supplicant enter");
	/* Make sure supplicant is running */
	if (!property_get(WPA_SUPP_PROP_NAME, supp_status, NULL)
		|| strcmp(supp_status, "running") != 0) {
		LOGE("Supplicant not running, cannot connect");
		return -1;
	}

	#if 0
	#ifdef USE_DRIVER_PROP_IF_NAME
	getWifiIfname(defIfname);
	property_get(DRIVER_PROP_IF_NAME, ifname, defIfname);
	#else
	getWifiIfname(ifname);
	#endif
	#endif

	if(*getWifiIfname() == '\0') {
		LOGD("wifi_connect_to_supplicant: getWifiIfname fail");
		return -1;
	}

	snprintf(ctrl_conn_path, sizeof(ctrl_conn_path), "%s/%s", WPA_SUPP_CTRL_DIR, getWifiIfname());
	LOGD("ctrl_conn_path = %s\n", ctrl_conn_path);
	
	{ /* check iface file is ready */
		int cnt = 160; /* 8 seconds (160*50)*/
		sched_yield();
		while ( access(ctrl_conn_path, F_OK|W_OK)!=0 && cnt-- > 0) {
			usleep(50000);
		}
		if (access(ctrl_conn_path, F_OK|W_OK)==0) {
			LOGD("ctrl_conn_path %s is ready to read/write cnt=%d\n", ctrl_conn_path, cnt);
		} else {
			LOGD("ctrl_conn_path %s is not ready, cnt=%d\n", ctrl_conn_path, cnt);
		}
	}
	
	while (retry_times--){
		ctrl_conn = wpa_ctrl_open(ctrl_conn_path);
		if (NULL == ctrl_conn) {
			usleep(1000 * 500);
			LOGD("Retry to wpa_ctrl_open \n");
		} else {
			break;
		}
	}
	
	if (NULL == ctrl_conn) {
		LOGE("Unable to open connection to supplicant on \"%s\": %s",
		ctrl_conn_path, strerror(errno));
		return -1;
	}
   
    monitor_conn = wpa_ctrl_open(ctrl_conn_path);
    if (monitor_conn == NULL) {
        wpa_ctrl_close(ctrl_conn);
        ctrl_conn = NULL;
        return -1;
    }
    if (wpa_ctrl_attach(monitor_conn) != 0) {
        wpa_ctrl_close(monitor_conn);
        wpa_ctrl_close(ctrl_conn);
        ctrl_conn = monitor_conn = NULL;
        return -1;
    }
    return 0;
}

int wifi_send_command(struct wpa_ctrl *ctrl, const char *cmd, char *reply, size_t *reply_len)
{
    int ret;

    if (ctrl_conn == NULL) {
        LOGV("Not connected to wpa_supplicant - \"%s\" command dropped.\n", cmd);
        return -1;
    }
    ret = wpa_ctrl_request(ctrl, cmd, strlen(cmd), reply, reply_len, NULL);
    if (ret == -2) {
        LOGD("'%s' command timed out.\n", cmd);
        return -2;
    } else if (ret < 0 || strncmp(reply, "FAIL", 4) == 0) {
        return -1;
    }
    if (strncmp(cmd, "PING", 4) == 0) {
        reply[*reply_len] = '\0';
    }
    LOGD("command: %s",cmd);
    LOGD("reply: %s", reply);
    return 0;
}

int wifi_wait_for_event(char *buf, size_t buflen)
{
    size_t nread = buflen - 1;
    int fd;
    fd_set rfds;
    int result;
    struct timeval tval;
    struct timeval *tptr;
    
    if (monitor_conn == NULL)
        return 0;

    result = wpa_ctrl_recv(monitor_conn, buf, &nread);
    if (result < 0) {
        LOGD("wpa_ctrl_recv failed: %s\n", strerror(errno));
        return -1;
    }
    buf[nread] = '\0';
    /* LOGD("wait_for_event: result=%d nread=%d string=\"%s\"\n", result, nread, buf); */
    /* Check for EOF on the socket */
    if (result == 0 && nread == 0) {
        /* Fabricate an event to pass up */
        LOGD("Received EOF on supplicant socket\n");
        strncpy(buf, WPA_EVENT_TERMINATING " - signal 0 received", buflen-1);
        buf[buflen-1] = '\0';
        return strlen(buf);
    }
    /*
     * Events strings are in the format
     *
     *     <N>CTRL-EVENT-XXX 
     *
     * where N is the message level in numerical form (0=VERBOSE, 1=DEBUG,
     * etc.) and XXX is the event name. The level information is not useful
     * to us, so strip it off.
     */
    if (buf[0] == '<') {
        char *match = strchr(buf, '>');
        if (match != NULL) {
            nread -= (match+1-buf);
            memmove(buf, match+1, nread+1);
        }
    }
    return nread;
}

void wifi_close_supplicant_connection()
{
    if (ctrl_conn != NULL) {
        wpa_ctrl_close(ctrl_conn);
        ctrl_conn = NULL;
    }
    if (monitor_conn != NULL) {
        wpa_ctrl_close(monitor_conn);
        monitor_conn = NULL;
    }
}

int wifi_command(const char *command, char *reply, size_t *reply_len)
{
    return wifi_send_command(ctrl_conn, command, reply, reply_len);
}

#define WIFI_DEFAULT_BI         100         /* in TU */
#define WIFI_DEFAULT_DTIM       1           /* in beacon */
#define WIFI_DEFAULT_CHANNEL    6
#define WIFI_DEFAULT_MAX_STA    8
#define WIFI_DEFAULT_PREAMBLE   0

static struct wpa_ctrl *hostapd_ctrl_conn = NULL;
static int hostapd_conif_valid = 0;

int set_hostapd_config_file(int argc, char *argv[])
{
	int fd;
	char buf[80];
	int len;
	hostapd_conif_valid = 0;

	fd = open(HOSTAPD_CONFIG_FILE, O_CREAT|O_WRONLY|O_TRUNC, 0660);
	if (fd < 0) {
		LOGE("Cannot create \"%s\": %s", HOSTAPD_CONFIG_FILE, strerror(errno));
		return -1;
	}

	#if 0
	#ifdef USE_DRIVER_PROP_IF_NAME
	getWifiIfname(defIfname);
	property_get(DRIVER_PROP_IF_NAME, ifname, defIfname);
	#else
	getWifiIfname(ifname);
	#endif
	#endif


	if(*getWifiIfname() == '\0') {
		LOGD("set_hostapd_config_file: getWifiIfname fail");
		return -1;
	}
	
	//len = snprintf(buf, sizeof(buf), "interface=%s\n",argv[3]);
	len = snprintf(buf, sizeof(buf), "interface=%s\n", getWifiIfname());
	write(fd, buf, len);
	len = snprintf(buf, sizeof(buf), "ctrl_interface=%s\n", HOSTAPD_CTRL_DIR);
	write(fd, buf, len);

	/* for CU-series flag */
	len = snprintf(buf, sizeof(buf), "driver=rtl871xdrv\n");
	write(fd, buf, len);
	len = snprintf(buf, sizeof(buf), "wme_enabled=1\n");
	write(fd, buf, len);
	len = snprintf(buf, sizeof(buf), "hw_mode=g\n");
	write(fd, buf, len);
	len = snprintf(buf, sizeof(buf), "ieee80211n=1\n");
	write(fd, buf, len);
	len = snprintf(buf, sizeof(buf), "ht_capab=[SHORT-GI-20][SHORT-GI-40]\n");
	write(fd, buf, len);
    
	if (argc > 4) {
		len = snprintf(buf, sizeof(buf), "ssid=%s\n",argv[4]);
	} else {
		len = snprintf(buf, sizeof(buf), "ssid=AndroidAP\n");
	}
	write(fd, buf, len);
	
	/* set open auth */
	len = snprintf(buf, sizeof(buf), "auth_algs=1\n");
	write(fd, buf, len);
	len = snprintf(buf, sizeof(buf), "max_num_sta=%d\n", WIFI_DEFAULT_MAX_STA);
	write(fd, buf, len);
	len = snprintf(buf, sizeof(buf), "beacon_int=%d\n", WIFI_DEFAULT_BI);
	write(fd, buf, len);
	len = snprintf(buf, sizeof(buf), "dtim_period=%d\n", WIFI_DEFAULT_DTIM);
	write(fd, buf, len);
	
	if (argc > 5) {
		if (strncmp(argv[5], "wpa2-psk", 8) == 0) {
			len = snprintf(buf, sizeof(buf), "wpa=2\n");
			write(fd, buf, len);
			len = snprintf(buf, sizeof(buf), "wpa_key_mgmt=WPA-PSK\n");
			write(fd, buf, len);
			len = snprintf(buf, sizeof(buf), "wpa_pairwise=CCMP\n");
			write(fd, buf, len);
			
			if (argc > 6) {
				len = snprintf(buf, sizeof(buf), "wpa_passphrase=%s\n",argv[6]);
				write(fd, buf, len);
			} else {
				len = snprintf(buf, sizeof(buf), "wpa_passphrase=12345678\n");
				write(fd, buf, len);
			}
		}
	}
	
	if (argc > 7) {
		len = snprintf(buf, sizeof(buf), "channel=%s\n",argv[7]);
		write(fd, buf, len);
	} else {
		len = snprintf(buf, sizeof(buf), "channel=%d\n",WIFI_DEFAULT_CHANNEL);
		write(fd, buf, len);
	}
	
	if (argc > 8) {
		len = snprintf(buf, sizeof(buf), "preamble=%s\n",argv[8]);
		write(fd, buf, len);
	} else {
		len = snprintf(buf, sizeof(buf), "preamble=%d\n",WIFI_DEFAULT_PREAMBLE);
		write(fd, buf, len);
	}

	len = snprintf(buf, sizeof(buf), "eap_server=1\n");
	write(fd, buf, len);

	len = snprintf(buf, sizeof(buf), "wps_state=2\n");
	write(fd, buf, len);
	
	hostapd_conif_valid = 1;
	close(fd);

	return 0;
}

#ifdef START_HOSTAPD_INSIDE
int wifi_start_hostapd()
{
	static int hostapdPid=0;
	
	LOGD("SoftapController::wifi_start_hostapd");


	/* Check whether already running */
	if (hostapdPid != 0) {
		LOGE("%s already started", HOSTAPD_NAME);
		return 0;
	}

	/* Clear out any stale socket files that might be left over. */
	wpa_ctrl_cleanup();
	
	LOGD("Starting %s services", HOSTAPD_NAME);


	if ((pid = fork()) < 0) {
		LOGE("fork failed (%s)", strerror(errno));
		return -1;
	}

	if (!pid) {
		
		char *args[] = {
			(char *)"/system/bin/hostapd"
			(char *)HOSTAPD_CONFIG_FILE	
			,(char *) 0 
		};

		if (execv(args[0], args)) {
			LOGE("execl failed (%s)", strerror(errno));
		}

		LOGE("Should never get here!");
		return 0;
	} else {
		hostapdPid = pid;
		LOGD("%s services running", HOSTAPD_NAME);
	}

    return 0;

}

int wifi_stop_hostapd()
{
	int count = 50; /* wait at most 5 seconds for completion */
	int wait_ret;

	/* Check whether hostapd already stopped */
	if (hostapdPid == 0) {
		LOGE("%s already stopped", HOSTAPD_NAME);
		return 0;
	}
	

	LOGD("Stopping %s services", HOSTAPD_NAME);
	kill(hostapdPid, SIGTERM);

	while (count-- > 0) {	
		if(hostapdPid == (wait_ret=waitpid(hostapdPid, NULL, WNOHANG)) ) {
			LOGD("%s services stopped", HOSTAPD_NAME);
			hostapdPid = 0;
			return 0
		} else if(wait_ret == 0) {
			usleep(100000);
		} else {
			LOGD("Stopping %s services failed(%s)", HOSTAPD_NAME, strerror(errno));
			return -1;
		}
	}
	return -1;
		
}
#else
int wifi_start_hostapd()
{
    char daemon_cmd[PROPERTY_VALUE_MAX];
    char supp_status[PROPERTY_VALUE_MAX] = {'\0'};
    int count = 200; /* wait at most 20 seconds for completion */
    char mac_buff[15] = {'\0'};
#ifdef HAVE_LIBC_SYSTEM_PROPERTIES
    const prop_info *pi;
    unsigned serial = 0;
#endif

	LOGD("SoftapController::wifi_start_hostapd");

	/* Check whether already running */
	if (property_get(HOSTAPD_PROP_NAME, supp_status, NULL)
		&& strcmp(supp_status, "running") == 0) {
		return 0;
	}

	/* Before starting the daemon, make sure its config file exists */
	if(ensure_config_file_exists(HOSTAPD_CONFIG_FILE, HOSTAPD_CONFIG_TEMPLATE) < 0) {
		LOGE("configuration file missing");
		return -1;
	}

	/* Clear out any stale socket files that might be left over. */
	wpa_ctrl_cleanup();

#ifdef HAVE_LIBC_SYSTEM_PROPERTIES
	/*
	 * Get a reference to the status property, so we can distinguish
	 * the case where it goes stopped => running => stopped (i.e.,
	 * it start up, but fails right away) from the case in which
	 * it starts in the stopped state and never manages to start
	 * running at all.
	 */
	pi = __system_property_find(HOSTAPD_PROP_NAME);
	if (pi != NULL) {
		serial = pi->serial;
	}
#endif

	#ifdef CONFIG_DAEMON_CMD_WITH_PARA
	snprintf(daemon_cmd, PROPERTY_VALUE_MAX, "%s:%s", HOSTAPD_NAME, HOSTAPD_CONFIG_FILE);
	#else
	snprintf(daemon_cmd, PROPERTY_VALUE_MAX, "%s", HOSTAPD_NAME);
	#endif
	
	property_set("ctl.start", daemon_cmd);
	LOGD("hostapd daemon_cmd = %s\n", daemon_cmd);   
	sched_yield();

	while (count-- > 0) {
#ifdef HAVE_LIBC_SYSTEM_PROPERTIES
		if (pi == NULL) {
			pi = __system_property_find(HOSTAPD_PROP_NAME);
		}
		if (pi != NULL) {
			__system_property_read(pi, NULL, supp_status);
			if (strcmp(supp_status, "running") == 0) {
				LOGD("hostapd running 1");
				return 0;
			} else if (pi->serial != serial && strcmp(supp_status, "stopped") == 0) {
				LOGI("wifi_start_supplicant stopped pi->serial %u serial %u leave", pi->serial, serial);
				if (serial==0) { /* no initialized, skip it */
					serial = pi->serial;
				} else {
					LOGE("HAVE_LIBC_SYSTEM_PROPERTIES: return -1");
					return -1;
				}
			}
		}
#else
		if (property_get(HOSTAPD_PROP_NAME, supp_status, NULL)) {
			if (strcmp(supp_status, "running") == 0) {
				LOGD("hostapd running 2");
				return 0;
			}
		}
#endif
		usleep(100000);
	}
	LOGI("wifi_start_hostapd is NOT running. timeout. leave" );
	return -1;
}

int wifi_stop_hostapd()
{
	char supp_status[PROPERTY_VALUE_MAX] = {'\0'};
	int count = 50; /* wait at most 5 seconds for completion */

	/* Check whether hostapd already stopped */
	if (property_get(HOSTAPD_PROP_NAME, supp_status, NULL)
		&& strcmp(supp_status, "stopped") == 0) {
		return 0;
	}

	property_set("ctl.stop", HOSTAPD_NAME);
	sched_yield();

	while (count-- > 0) {
		if (property_get(HOSTAPD_PROP_NAME, supp_status, NULL)) {
			if (strcmp(supp_status, "stopped") == 0)
				return 0;
		}
		usleep(100000);
	}
	return -1;
}
#endif

int wifi_connect_to_hostapd()
{
	char defIfname[256];
	char ctrl_conn_path[256];
	char supp_status[PROPERTY_VALUE_MAX] = {'\0'};
	int retry_times = 20;

	/* Make sure hostapd is running */
	if (!property_get(HOSTAPD_PROP_NAME, supp_status, NULL)
		|| strcmp(supp_status, "running") != 0) {
		LOGE("hostapd not running, cannot connect");
		return -1;
	}

	#if 0
	#ifdef USE_DRIVER_PROP_IF_NAME
	getWifiIfname(defIfname);
	property_get(DRIVER_PROP_IF_NAME, ifname, defIfname);
	#else
	getWifiIfname(ifname);
	#endif
	#endif

	if(*getWifiIfname() == '\0') {
		LOGD("wifi_connect_to_hostapd: getWifiIfname fail");
		return -1;
	}
	
	snprintf(ctrl_conn_path, sizeof(ctrl_conn_path), "%s/%s", HOSTAPD_CTRL_DIR, getWifiIfname());
	LOGD("ctrl_conn_path = %s\n", ctrl_conn_path);


	{ /* check iface file is ready */
		int cnt = 160; /* 8 seconds (160*50)*/
		sched_yield();
		while ( access(ctrl_conn_path, F_OK|W_OK)!=0 && cnt-- > 0) {
			usleep(50000);
		}
		if (access(ctrl_conn_path, F_OK|W_OK)==0) {
			LOGD("ctrl_conn_path %s is ready to read/write cnt=%d\n", ctrl_conn_path, cnt);
		} else {
			LOGD("ctrl_conn_path %s is not ready, cnt=%d\n", ctrl_conn_path, cnt);
		}
	}

	if(hostapd_ctrl_conn)
		LOGE("before wpa_ctrl_open, hostapd_ctrl_conn is not NULL\n");

	while (retry_times--){
		hostapd_ctrl_conn = wpa_ctrl_open(ctrl_conn_path);
		if (NULL == hostapd_ctrl_conn) {
			usleep(1000 * 500);
			LOGD("Retry to wpa_ctrl_open \n");
		} else {
			break;
		}
	}
	
	if (NULL == hostapd_ctrl_conn) {
		LOGE("Unable to open connection to supplicant on \"%s\": %s",
		ctrl_conn_path, strerror(errno));
		return -1;
	}

	if (wpa_ctrl_attach(hostapd_ctrl_conn) != 0) {
		wpa_ctrl_close(hostapd_ctrl_conn);
		hostapd_ctrl_conn = NULL;
		return -1;
	}
	return 0;
}

void wifi_close_hostapd_connection()
{
	if (hostapd_ctrl_conn != NULL) {
		wpa_ctrl_close(hostapd_ctrl_conn);
		hostapd_ctrl_conn = NULL;
	}
}

int wifi_load_profile(int started)
{
	int ret;
	int retry=0;
	if ((started) && (hostapd_conif_valid)) {
retry:
		if (hostapd_ctrl_conn == NULL) {
			LOGE("wifi_load_profile(): hostapd_ctrl_conn == NULL");
			return -1;
		} else {
			if( (ret=wpa_ctrl_reload(hostapd_ctrl_conn))<0){
				if(retry++==0) {
					LOGE("wpa_ctrl_reload(hostapd_ctrl_conn) fail, retry...");
					wifi_close_hostapd_connection();
					wifi_connect_to_hostapd();
					goto retry;
				}
			}
			return ret;
		}
	}
	return 0;
}