// vk_probe.c
// Small Vulkan probe for VK_KHR_display: create instance, pick physical device,
// enumerate displays and display modes.
//
// Build: gcc -O2 -Wall -o vk_probe src/vk_probe.c -lvulkan
//
// Run: ./vk_probe
//
// Expect: list of physical devices and any VK_KHR_display displays + modes.

#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static void failf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}

static VkInstance create_instance(void)
{
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "ArgusVKProbe",
        .applicationVersion = VK_MAKE_VERSION(0,1,0),
        .pEngineName = "Argus",
        .engineVersion = VK_MAKE_VERSION(0,1,0),
        .apiVersion = VK_API_VERSION_1_0
    };

    // Request VK_KHR_surface and VK_KHR_display for display enumeration/presentation
    const char *exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_DISPLAY_EXTENSION_NAME
    };

    VkInstanceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .pApplicationInfo = &app,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = (uint32_t)(sizeof(exts)/sizeof(exts[0])),
        .ppEnabledExtensionNames = exts
    };

    VkInstance inst;
    VkResult r = vkCreateInstance(&ci, NULL, &inst);
    if (r != VK_SUCCESS) {
        failf("vkCreateInstance failed: %d\n", (int)r);
    }
    return inst;
}

static void list_displays(VkInstance inst)
{
    uint32_t gpu_count = 0;
    VkResult r = vkEnumeratePhysicalDevices(inst, &gpu_count, NULL);
    if (r != VK_SUCCESS || gpu_count == 0) {
        fprintf(stderr, "No Vulkan physical devices found (vkEnumeratePhysicalDevices -> %d, count=%u)\n", (int)r, gpu_count);
        return;
    }

    VkPhysicalDevice *gpus = calloc(gpu_count, sizeof(*gpus));
    if (!gpus) failf("calloc out of memory\n");
    r = vkEnumeratePhysicalDevices(inst, &gpu_count, gpus);
    if (r != VK_SUCCESS) failf("vkEnumeratePhysicalDevices second call failed: %d\n", (int)r);

    printf("Found %u physical device(s)\n\n", gpu_count);

    for (uint32_t gi = 0; gi < gpu_count; ++gi) {
        VkPhysicalDevice phys = gpus[gi];
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(phys, &props);
        printf("GPU %u: %s (apiVersion %u.%u.%u)\n",
               gi, props.deviceName,
               VK_VERSION_MAJOR(props.apiVersion),
               VK_VERSION_MINOR(props.apiVersion),
               VK_VERSION_PATCH(props.apiVersion));

        // Check if VK_KHR_display extension is supported by this physical device
        uint32_t ext_count = 0;
        r = vkEnumerateDeviceExtensionProperties(phys, NULL, &ext_count, NULL);
        if (r != VK_SUCCESS) {
            fprintf(stderr, "  vkEnumerateDeviceExtensionProperties failed: %d\n", (int)r);
            continue;
        }
        VkExtensionProperties *exts = calloc(ext_count, sizeof(*exts));
        if (!exts) failf("calloc out of memory\n");
        r = vkEnumerateDeviceExtensionProperties(phys, NULL, &ext_count, exts);
        if (r != VK_SUCCESS) {
            fprintf(stderr, "  vkEnumerateDeviceExtensionProperties failed 2: %d\n", (int)r);
            free(exts);
            continue;
        }
        int has_display = 0;
        for (uint32_t ei = 0; ei < ext_count; ++ei) {
            if (strcmp(exts[ei].extensionName, VK_KHR_DISPLAY_EXTENSION_NAME) == 0) {
                has_display = 1;
                break;
            }
        }
        printf("  device extensions: %u (VK_KHR_display %s)\n", ext_count, has_display ? "available" : "missing");
        free(exts);

        if (!has_display) {
            printf("    -> skipping display enumerate for this device\n\n");
            continue;
        }

        // Enumerate displays
        uint32_t disp_count = 0;
        PFN_vkGetPhysicalDeviceDisplayPropertiesKHR vkGetPhyDispProps =
            (PFN_vkGetPhysicalDeviceDisplayPropertiesKHR) vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceDisplayPropertiesKHR");
        if (!vkGetPhyDispProps) {
            fprintf(stderr, "  vkGetPhysicalDeviceDisplayPropertiesKHR not available\n");
            continue;
        }

        r = vkGetPhyDispProps(phys, &disp_count, NULL);
        if (r != VK_SUCCESS) {
            fprintf(stderr, "  vkGetPhysicalDeviceDisplayPropertiesKHR failed: %d\n", (int)r);
            continue;
        }
        if (disp_count == 0) {
            printf("  no displays exposed via VK_KHR_display\n\n");
            continue;
        }

        VkDisplayPropertiesKHR *disp_props = calloc(disp_count, sizeof(*disp_props));
        if (!disp_props) failf("calloc out of memory\n");
        r = vkGetPhyDispProps(phys, &disp_count, disp_props);
        if (r != VK_SUCCESS) {
            fprintf(stderr, "  vkGetPhysicalDeviceDisplayPropertiesKHR(second) failed: %d\n", (int)r);
            free(disp_props);
            continue;
        }

        for (uint32_t di = 0; di < disp_count; ++di) {
            VkDisplayPropertiesKHR *dp = &disp_props[di];
            printf("  Display %u: name='%s' physicalSize=%dx%dmm (planeReorderPossible=%u persistent=%u)\n",
                   di,
                   dp->displayName ? dp->displayName : "(null)",
                   dp->physicalDimensions.width, dp->physicalDimensions.height,
                   dp->planeReorderPossible, dp->persistentContent);

            // Enumerate available display modes for this display
            PFN_vkGetDisplayModePropertiesKHR vkGetModeProps =
                (PFN_vkGetDisplayModePropertiesKHR) vkGetInstanceProcAddr(inst, "vkGetDisplayModePropertiesKHR");
            if (!vkGetModeProps) {
                fprintf(stderr, "    vkGetDisplayModePropertiesKHR not available\n");
                continue;
            }
            uint32_t mode_count = 0;
            r = vkGetModeProps(phys, dp->display, &mode_count, NULL);
            if (r != VK_SUCCESS) {
                fprintf(stderr, "    vkGetDisplayModePropertiesKHR failed: %d\n", (int)r);
                continue;
            }
            VkDisplayModePropertiesKHR *mprops = calloc(mode_count, sizeof(*mprops));
            if (!mprops) failf("calloc out of memory\n");
            r = vkGetModeProps(phys, dp->display, &mode_count, mprops);
            if (r != VK_SUCCESS) {
                fprintf(stderr, "    vkGetDisplayModePropertiesKHR(second) failed: %d\n", (int)r);
                free(mprops);
                continue;
            }
            for (uint32_t mi = 0; mi < mode_count; ++mi) {
                VkDisplayModeParametersKHR *p = &mprops[mi].parameters;
                printf("    Mode %u: visibleRegion=%ux%u refresh=%u/%u\n",
                       mi, p->visibleRegion.width, p->visibleRegion.height, p->refreshRate, 1000);
            }
            free(mprops);
        }
        free(disp_props);

        printf("\n");
    }

    free(gpus);
}

int main(int argc, char **argv)
{
    (void) argc; (void) argv;

    VkInstance inst = create_instance();
    if (!inst) failf("failed to create instance\n");

    list_displays(inst);

    vkDestroyInstance(inst, NULL);
    return 0;
}
