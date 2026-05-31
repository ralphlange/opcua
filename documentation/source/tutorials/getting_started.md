# Getting Started with OPC UA Device Support

This tutorial guides you through the process of building the OPC UA device support and setting up a basic EPICS IOC connection.

## Building the Software

To use the OPC UA Device Support, you first need to build the module and its dependencies.

### Prerequisites

*   **EPICS Base:** A functioning EPICS Base installation (R7.0.6 or newer recommended).
*   **Unified Automation C++ SDK:** The OPC UA Device Support relies on the Unified Automation C++ SDK.
    *   Download the free evaluation version from [Unified Automation](https://www.unified-automation.com/downloads/opc-ua-sdks.html).
    *   For production use, a commercial license is required.
    *   Extract the SDK to a known location, e.g., `/opt/unifiedautomation/UaSdkCppBundle` or `C:\Program Files\UnifiedAutomation\UaSdkCppBundle`.
    *   Set the environment variable `UA_SDK_HOME` to point to this directory.

### Building `opcua-device-support`

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/ralphlange/opcua.git
    cd opcua
    ```

2.  **Configure `configure/RELEASE`:**
    *   Open `configure/RELEASE` and adjust paths for `EPICS_BASE` and `UA_SDK_HOME` if they are not set as environment variables or if their locations differ from common defaults.
    *   Example `RELEASE` snippet:
        ```
        EPICS_BASE=/path/to/epics/base
        UA_SDK_HOME=/path/to/unifiedautomation/UaSdkCppBundle
        ```

3.  **Build the module:**
    ```bash
    make
    ```

    If the build is successful, you will find the `libopcua.a` (or `.so`) in `lib/<arch>`.

## Basic Configuration (EPICS IOC)

This section provides a simple example to configure an EPICS IOC to communicate with an OPC UA server.

### Create an EPICS IOC Application

1.  **Create a new EPICS application:**
    ```bash
    cd <your_epics_area>
    /path/to/epics/base/bin/<arch>/makeBaseApp.pl -t ioc <your_ioc_name>
    /path/to/epics/base/bin/<arch>/makeBaseApp.pl -i -t ioc <your_ioc_name>
    cd <your_ioc_name>App/src
    ```

2.  **Add `opcua` to your `Makefile`:**
    Edit `<your_ioc_name>App/src/Makefile` and add `opcua` to `PROD_LIBS`.
    ```makefile
    <your_ioc_name>_LIBS += opcua
    ```

### Configure an OPC UA Session

An OPC UA session is a named connection between the EPICS client (IOC) and the OPC UA server.

1.  **Define Session Parameters:**
    You will typically define session parameters in your IOC's startup script (e.g., `st.cmd`).
    The `opcuaSession` command is used to create and configure a session.

    ```
    # Create an OPC UA session named "mySession"
    # Connects to opc.tcp://localhost:4840
    # Automatically tries to reconnect if disconnected
    opcuaSession "mySession", "opc.tcp://localhost:4840", "AutoConnect=YES"
    ```

    *   `"mySession"`: A unique name for your session.
    *   `"opc.tcp://localhost:4840"`: The URL of your OPC UA server. Replace `localhost` and `4840` with your server's actual address and port.
    *   `"AutoConnect=YES"`: Configures the IOC to automatically attempt re-establishment of the connection if it's lost.

### Configure an OPC UA Subscription

Subscriptions allow the client to receive data updates from the server without constant polling.

1.  **Define Subscription Parameters:**
    Use the `opcuaSubscription` command in your `st.cmd`.

    ```
    # Create a subscription named "fastUpdates" for "mySession"
    # Publishing interval of 100ms
    opcuaSubscription "fastUpdates", "mySession", 100
    ```

    *   `"fastUpdates"`: A unique name for this subscription.
    *   `"mySession"`: The name of the session this subscription belongs to.
    *   `100`: The publishing interval in milliseconds.

## Creating EPICS Records for OPC UA Items

Now, let's create a simple EPICS record that reads a boolean value from an OPC UA server.

1.  **Create a database file:**
    Create a file named `db/example.db` in your IOC application directory.

2.  **Define a `bi` (Binary Input) record:**
    ```
    record(bi, "$(P)MyBoolean") {
        field(DTYP, "OPCUA")
        field(INP, "@mySession ns=3;s=\"MyDataBlock\".\"MyBooleanItem\"")
        field(SCAN, "I/O Intr")
        field(ZNAM, "FALSE")
        field(ONAM, "TRUE")
    }
    ```

    *   `$(P)MyBoolean`: The process variable (PV) name. `$(P)` is a prefix defined in your `st.cmd`.
    *   `DTYP, "OPCUA"`: Specifies that this record uses the OPC UA device support.
    *   `INP, "@mySession ns=3;s=\"MyDataBlock\".\"MyBooleanItem\""`: This is the crucial link field.
        *   `@mySession`: The name of the OPC UA session to use.
        *   `ns=3`: The namespace index of the OPC UA item.
        *   `s=\"MyDataBlock\".\"MyBooleanItem\""`: The string identifier of the OPC UA item. Note the escaped quotation marks for identifiers containing special characters or periods.
    *   `SCAN, "I/O Intr"`: Configures the record to process upon reception of a publishing event from the OPC UA server, which is efficient for subscriptions.
    *   `ZNAM, "FALSE"` and `ONAM, "TRUE"`: User-friendly names for the binary states.

### Loading the Database in `st.cmd`

Add the following to your `st.cmd` to load the database:

```
dbLoadRecords("db/example.db", "P=MY_IOC:")
```

## Running the IOC

1.  **Start your OPC UA Server:** Ensure your Siemens S7-1500 PLC (or any other OPC UA server) is running and configured to expose the OPC UA items you intend to access.
2.  **Start your EPICS IOC:**
    ```bash
    cd <your_ioc_name>App/src
    ./st.cmd
    ```

3.  **Verify Connection:**
    Observe the IOC console output for messages indicating successful connection to the OPC UA server and subscription setup. You can also use `caget` and `caput` to interact with your PVs.

    ```bash
    caget MY_IOC:MyBoolean
    ```

    You should see the value of `MyBooleanItem` from your OPC UA server.
