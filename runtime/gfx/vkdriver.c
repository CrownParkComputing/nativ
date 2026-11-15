/*
 * Which Vulkan driver the game runs on (settings key "vulkan_driver"; the overlay's Display page).
 *
 * An Android handheld's own Adreno driver is usually years behind: a Retroid Pocket Flip2's reports
 * Vulkan 1.1, where Mesa's Turnip reports 1.3. A player can drop a newer Turnip build beside the
 * game's files and pick it in the menu, without a new package and without this project shipping
 * anyone else's driver.
 *
 * Drivers live in `drivers/` under the game's own files directory (dev/android-install.sh pushes
 * one there, and nothing stops a player adding more) -- NOT inside the package, so a new Turnip
 * release is one adb push away. Android refuses to dlopen code from there in the ordinary way;
 * libadrenotools loads it through a memfd instead, which is what platform/android/vkshim.c is for.
 *
 * The choice cannot be applied while the game runs: SDL opens the Vulkan library once, before there
 * is a device, and changing it afterwards would mean rebuilding every pipeline, texture and buffer.
 * So vkdriver_apply() runs at startup (main.c, after the settings are loaded and before the window
 * opens) and the menu says a restart is needed.
 *
 * Elsewhere than Android there is no shim and no list: the menu offers the system driver alone and
 * says why.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "settings.h"
#include "vkdriver.h"

#ifdef __ANDROID__
#include <dirent.h>
#endif

static char current[VKDRIVER_NAME_MAX] = "system";
const char *vkdriver_current(void) { return current; }

const char *vkdriver_dir(void) {
#ifdef __ANDROID__
	static char dir[1024];
	if (!dir[0]) {
		const char *ext = SDL_GetAndroidExternalStoragePath();
		if (!ext) return "";
		snprintf(dir, sizeof dir, "%s/drivers", ext);
	}
	return dir;
#else
	/* RECOMP_VULKAN_DRIVER_DIR still names one, so the list and the menu can be exercised on a
	 * desktop; without the shim nothing there would actually load. */
	const char *e = getenv("RECOMP_VULKAN_DRIVER_DIR");
	return e ? e : "";
#endif
}

static int by_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

int vkdriver_list(char (*names)[VKDRIVER_NAME_MAX], int max) {
	const char *dir = vkdriver_dir();
	if (!dir || !*dir || max <= 0) return 0;
	int n = 0;
#ifdef __ANDROID__
	DIR *d = opendir(dir);
	if (!d) return 0;
	for (struct dirent *e; n < max && (e = readdir(d));) {
		const char *s = e->d_name, *dot = strrchr(s, '.');
		if (!dot || strcmp(dot, ".so") || s[0] == '.') continue;
		snprintf(names[n++], VKDRIVER_NAME_MAX, "%s", s);
	}
	closedir(d);
#else
	(void)names;
#endif
	if (n > 1) qsort(names, (size_t)n, VKDRIVER_NAME_MAX, by_name);
	return n;
}

void vkdriver_apply(void) {
	/* The setting is the player's choice and wins, "system" included -- that is the point of the
	 * menu. With no setting at all, whatever the launcher or launch.ini put in the environment
	 * stands, so an install that pinned a driver keeps working until the menu is used once. */
	char want[VKDRIVER_NAME_MAX] = "";
	settings_str("vulkan_driver", "", want, sizeof want);
	if (!want[0]) {
		const char *env = getenv("RECOMP_VULKAN_DRIVER");
		snprintf(current, sizeof current, "%s", env && *env ? env : "system");
		return;
	}
	if (!strcmp(want, "system")) {
		unsetenv("SDL_VULKAN_LIBRARY");   /* SDL then opens the platform's own libvulkan */
		unsetenv("RECOMP_VULKAN_DRIVER");
		unsetenv("RECOMP_VULKAN_DRIVER_DIR");
		snprintf(current, sizeof current, "system");
		return;
	}
	const char *dir = vkdriver_dir();
	char path[1280];
	snprintf(path, sizeof path, "%s/%s", dir && *dir ? dir : ".", want);
	FILE *f = fopen(path, "rb");   /* gone (a driver deleted since it was chosen): say so and run on the system one */
	if (!f) {
		fprintf(stderr, "    vulkan driver: %s is chosen but not in %s -- using the system driver\n", want, dir && *dir ? dir : "(nowhere)");
		unsetenv("SDL_VULKAN_LIBRARY"); unsetenv("RECOMP_VULKAN_DRIVER"); unsetenv("RECOMP_VULKAN_DRIVER_DIR");
		snprintf(current, sizeof current, "system");
		return;
	}
	fclose(f);
	setenv("RECOMP_VULKAN_DRIVER", want, 1);
	if (dir && *dir) setenv("RECOMP_VULKAN_DRIVER_DIR", dir, 1);
	setenv("SDL_VULKAN_LIBRARY", "libvkshim.so", 1);   /* the shim opens the driver; SDL cannot (vkshim.c) */
	snprintf(current, sizeof current, "%s", want);
	fprintf(stderr, "    vulkan driver: %s, from %s\n", want, dir);
}
