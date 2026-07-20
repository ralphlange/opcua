(deployment-modes)=
# Deployment Modes

The OPC UA Device Support module can be configured to link and deploy the low-level OPC UA SDK (either open62541 or Unified Automation UASDK) using different strategies. These strategies are referred to as **Deployment Modes**.

The deployment mode is controlled by setting the `OPEN62541_DEPLOY_MODE` or `UASDK_DEPLOY_MODE` variable in the `CONFIG_SITE.local` file of the Device Support module.

## Supported Modes

The following four modes are supported:

### `SYSTEM`
The shared libraries of the SDK are expected to be in a system-standard location (e.g., `/usr/lib` or `/usr/local/lib`). The build system uses these libraries, and they must be present on the target system at runtime.

### `PROVIDED`
The shared libraries of the SDK are located in a specific directory defined by `OPEN62541_SHRLIB_DIR` or `UASDK_SHRLIB_DIR`. These are used during the build, and the target system's runtime environment (e.g., `LD_LIBRARY_PATH` on Linux or `PATH` on Windows) must be configured to find them.

### `INSTALL`
The shared libraries of the SDK are copied (installed) into the Device Support module's own library/binary directory during the build process. This makes the module more self-contained as it carries its own copies of the required SDK libraries.

### `EMBED`
The static libraries of the SDK are linked directly into the `opcua` shared library. The SDK code becomes an integral part of `libopcua.so` (on Linux) or `opcua.dll` (on Windows). Consequently, the original SDK libraries are not required on the target system.

## Recommendations

The suggested Deployment Mode depends on the operating system and the desired linking strategy for the EPICS application.

### Windows

On Windows, the recommendation depends on the linkage chosen for the EPICS application:

- **Static Linking:** `INSTALL` is preferred. This helps avoid "DLL hell" by ensuring that the necessary dependency libraries are copied to the application's binary directory.
- **Dynamic Linking:** `EMBED` is suggested for simplicity. Since the low-level client is embedded into the Device Support library, no extra installation or management of the client library is required on the target system.

### Linux

On Linux, the recommendation depends on how the low-level library is managed on the system:

- **System Packages:** Use `SYSTEM` when the low-level library has been installed using the system's package manager (such as `apt` or `yum`).
- **Custom Location:** Use `PROVIDED` when the low-level library is deployed in a specific, non-standard location.
- **Simplicity:** `EMBED` can be used for simplicity, particularly when the OPC UA Device Support is the only component on the system using that specific low-level library.
