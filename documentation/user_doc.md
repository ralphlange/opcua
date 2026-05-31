# OPC UA Device Support Module Documentation

## 1. Tutorial: Getting Started with OPC UA Device Support

### 1.1 Building the Software

To use the OPC UA Device Support, you first need to build the module and its dependencies.

#### Prerequisites

*   **EPICS Base:** A functioning EPICS Base installation (R7.0.6 or newer recommended).
*   **Unified Automation C++ SDK:** The OPC UA Device Support relies on the Unified Automation C++ SDK.
    *   Download the free evaluation version from [Unified Automation](https://www.unified-automation.com/downloads/opc-ua-sdks.html).
    *   For production use, a commercial license is required.
    *   Extract the SDK to a known location, e.g., `/opt/unifiedautomation/UaSdkCppBundle` or `C:\Program Files\UnifiedAutomation\UaSdkCppBundle`.
    *   Set the environment variable `UA_SDK_HOME` to point to this directory.

#### Building `opcua-device-support`

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

### 1.2 Basic Configuration (EPICS IOC)

This section provides a simple example to configure an EPICS IOC to communicate with an OPC UA server.

#### Create an EPICS IOC Application

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

#### Configure an OPC UA Session

An OPC UA session is a named connection between the EPICS client (IOC) and the OPC UA server.

1.  **Define Session Parameters:**
    You will typically define session parameters in your IOC's startup script (e.g., `st.cmd`).
    The `opcuaSession` command is used to create and configure a session.

    ```
    # Create an OPC UA session named "mySession"
    # Connects to opc.tcp://localhost:4840
    # Automatically tries to reconnect if disconnected
    opcuaSessionAdd "mySession", "opc.tcp://localhost:4840", "AutoConnect=YES"
    ```

    *   `"mySession"`: A unique name for your session.
    *   `"opc.tcp://localhost:4840"`: The URL of your OPC UA server. Replace `localhost` and `4840` with your server's actual address and port.
    *   `"AutoConnect=YES"`: Configures the IOC to automatically attempt re-establishment of the connection if it's lost.

#### Configure an OPC UA Subscription

Subscriptions allow the client to receive data updates from the server without constant polling.

1.  **Define Subscription Parameters:**
    Use the `opcuaSubscriptionAdd` command in your `st.cmd`.

    ```
    # Create a subscription named "fastUpdates" for "mySession"
    # Publishing interval of 100ms
    opcuaSubscriptionAdd "mySession", "fastUpdates", "100"
    ```

    *   `"mySession"`: The name of the session this subscription belongs to.
    *   `"fastUpdates"`: A unique name for this subscription.
    *   `"100"`: The publishing interval in milliseconds.

### 1.3 Creating EPICS Records for OPC UA Items

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

#### Loading the Database in `st.cmd`

Add the following to your `st.cmd` to load the database:

```
dbLoadRecords("db/example.db", "P=MY_IOC:")
```

### 1.4 Running the IOC

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

## 2. How-To Guides

### 2.1 Configuring an OPC UA Server (Siemens S7-1500 PLC)

This guide details the steps to configure a Siemens S7-1500 PLC as an OPC UA server using TIA Portal.

#### 2.1.1 Activate OPC UA Server

1.  Open your project in TIA Portal.
2.  Navigate to your PLC's properties (e.g., `PLC_1 [CPU 1518-4 PN/DP]`).
3.  Go to `General` > `OPC UA` > `Server` > `General`.
4.  Check the `Activate OPC UA server` box.
5.  Ensure that one of the `Server addresses` (`opc.tcp://...`) matches the IP address of your PLC on the Process Operator Network (PON). The default OPC UA port is 4840.

    **Note:** The OPC UA server must be accessible via one of the PLC's IPs on the PON. It cannot be accessed via a separate PLC Communications Processor (CP) module.
    
#### 2.1.2 Configure OPC UA Server Options

1.  In the PLC properties, navigate to `General` > `OPC UA` > `Server` > `Options`.
2.  **Port:** Adjust the `Port` number if a different port than the default 4840 is required.
3.  **Minimum sampling interval:** Defines the minimum rate (in ms) at which the server checks data sources for changes.
4.  **Minimum publishing interval:** Defines the minimum rate (in ms) at which the server sends updated data to clients.

    **Note:** For S7-1500 PLCs, especially mid-size ones (e.g., 1516), starting with 250ms intervals and grouping variables into structures is a good practice for performance.
    
#### 2.1.3 Configure OPC UA Server Security

1.  In the PLC properties, navigate to `General` > `OPC UA` > `Server` > `Security`.
2.  **Security Policies:** Select the desired security policy. For basic testing and development, you might select `No security`. For production environments, stronger options like `Basic128Rsa15-Sign & Encrypt` are recommended.
3.  **Guest Authentication:** If no specific user authentication is required, check `Enable guest authentication`.

    **Warning:** OPC UA Security is enabled by default. To connect to any OPC UA server *without* security, you must set the option `sec-mode=None` for the concerned session(s) in your EPICS IOC configuration.

#### 2.1.4 Configure OPC UA Runtime License

1.  In the PLC properties, navigate to `General` > `Runtime licenses` > `OPC UA`.
2.  Ensure the `Type of purchased license` is at least the same level as the `Type of required license` (e.g., `SIMATIC OPC UA S7-1500 large`).

#### 2.1.5 Define Variables in PLC Data Structures

Organize your PLC variables into data blocks that will be exposed as OPC UA nodes.

1.  **Create Data Blocks:** In TIA Portal, create new data blocks (e.g., `DB1`, `DB2`, `DB3`, `DB4`).
2.  **Define Variables:** Inside each data block, define the variables you want to expose.
    *   **CFGU (Configuration, Unmonitored):** Variables transmitted from IOC to PLC without readback. No subscription needed.
    *   **CFGM (Configuration, Monitored):** Variables transmitted from IOC to PLC with readback if values change. Requires a subscription.
    *   **STSV (State, Single Value):** Variables transmitted from PLC to IOC, each independently. Requires a subscription.
    *   **STST (State, Structured):** Variables transmitted from PLC to IOC as a whole block if any element changes. Requires a subscription.

    **Example PLC Data Types and EPICS Record Type Compatibility:**

    | PLC data type         | EPICS Record type           |
    | :-------------------- | :-------------------------- |
    | REAL                  | ai/ao                       |
    | BOOL                  | bi/bo                       |
    | BYTE/WORD/DWORD/INT/DINT | longin/longout, mbbi/mbbo, mbbiDirect/mbboDirect, ai/ao |
    | STRUCT                | opcuaItem                   |

    **Note:** Structured items can only be handled by the IOC using the `opcuaItem` record type.

### 2.2 Configuring EPICS Records for Different Data Exchange Types

This guide shows how to configure EPICS records for various OPC UA data exchange scenarios.

#### 2.2.1 Single Value Data Exchanges

For individual OPC UA items mapped to single EPICS records.

*   **General INP/OUT Link Field Format:**
    `@<session_name> ns=<namespace_index>;<identifier_type>=<identifier> [<option>=<value>...]`

    *   `<session_name>`: Name of the OPC UA session.
    *   `<namespace_index>`: Numerical namespace index.
    *   `<identifier_type>`: `s` for string identifier, `i` for numerical identifier.
    *   `<identifier>`: The string or numerical ID of the OPC UA node. **Escape quotation marks** (`\"`) if the identifier string contains them (e.g., `"dataBlock"."myItem"` becomes `\"dataBlock\".\"myItem\"`).

*   **Common Options:**

    | Option     | Value                                         | Description                                                                                                                                                                                                               |
    | :--------- | :-------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
    | `sampling` | `double` (default: `-1` = use publishing interval) | Sampling interval in ms.                                                                                                                                                                                                  |
    | `qsize`    | `default: 1` = no queueing                    | Size of server-side queue.                                                                                                                                                                                                |
    | `cqsize`   | `default: 1.5 * qsize` (minimum 3)            | Size of client-side queue (needs to be > `qsize`).                                                                                                                                                                        |
    | `discard`  | `old` / `new` (default: `old` = drop oldest)  | Discard policy on queue overrun.                                                                                                                                                                                          |
    | `register` | `y` / `n` (default: `n` = do not register item) | Item registration with the server for better performance.                                                                                                                                                                 |
    | `timestamp`| `server` / `source` (default: `server`)       | Timestamp source.                                                                                                                                                                                                         |
    | `monitor`  | `y` / `n` (default: `y` = output record gets bidirectional) | Set up monitor. For output records, `y` enables readback.                                                                                                                                                                 |
    | `bini`     | `read` / `ignore` / `write` (default: `read`) | Behaviour at initialization when server reconnects: `read` (read value from server), `ignore` (read type but not update record), `write` (read type, then write record value to server). Use `bini` instead of `PINI` field. |

*   **Example: Config Unmonitored (CFGU) Variables (Output Records)**
    These are typically output records (`bo`, `ao`, `longout`, `mbbo`, `mbboDirect`) that send data to the PLC but don't require monitoring of their values changing on the PLC.

    ```
    # Boolean Output
    record(bo, "$(P)CFGU:BOOL") {
        field(DTYP, "OPCUA")
        field(OUT, "@PLC0 ns=3;s=\"CFGU\".\"BOOL\" monitor=n")
        field(ZNAM, "OFF")
        field(ONAM, "ON")
    }

    # Analog Output
    record(ao, "$(P)CFGU:REALA") {
        field(DTYP, "OPCUA")
        field(OUT, "@PLC0 ns=3;s=\"CFGU\".\"REALA\" monitor=n")
        field(DRVH, "100.0")
        field(DRVL, "0.0")
        field(HOPR, "90.0")
        field(LOPR, "10.0")
    }
    ```
    *   `monitor=n`: Explicitly disables monitoring for these unmonitored variables.

*   **Example: Config Monitored (CFGM) Variables (Output Records with Readback)**
    These are output records that send data to the PLC and also monitor for changes on the PLC, updating the record if the value changes. They require an OPC UA subscription.

    ```
    # Boolean Output (Monitored)
    record(bo, "$(P)CFGM:BOOL") {
        field(DTYP, "OPCUA")
        field(OUT, "@SLOW ns=3;s=\"CFGM\".\"BOOL\"")
        field(ZNAM, "OFF")
        field(ONAM, "ON")
    }

    # Analog Output (Monitored)
    record(ao, "$(P)CFGM:REALA") {
        field(DTYP, "OPCUA")
        field(OUT, "@SLOW ns=3;s=\"CFGM\".\"REALA\"")
        field(DRVH, "100.0")
        field(DRVL, "0.0")
        field(HOPR, "90.0")
        field(LOPR, "10.0")
    }
    ```
    *   `@SLOW`: The name of the subscription to which this item belongs. The session is implicitly defined by the subscription.
    *   `monitor=y` is the default for input records or for output records expecting readback, so it is often omitted.

*   **Example: State Single Variables (STSV) (Input Records)**
    These are input records (`bi`, `ai`, `longin`, `mbbi`, `mbbiDirect`) that read data from the PLC. They require an OPC UA subscription and should be `I/O Intr` scanned.

    ```
    # Binary Input
    record(bi, "$(P)STSV:BOOL") {
        field(DTYP, "OPCUA")
        field(INP, "@SLOW ns=3;s=\"STSV\".\"BOOL\"")
        field(SCAN, "I/O Intr")
        field(ZNAM, "OFF")
        field(ONAM, "ON")
    }

    # Analog Input
    record(ai, "$(P)STSV:REALA") {
        field(DTYP, "OPCUA")
        field(INP, "@SLOW ns=3;s=\"STSV\".\"REALA\"")
        field(SCAN, "I/O Intr")
    }
    ```
    *   `SCAN, "I/O Intr"`: Crucial for input records to process on data changes received via subscription, minimizing network traffic.

#### 2.2.2 Structured Data Exchanges

For exchanging entire data structures (blocks) between the IOC and OPC UA server using the `opcuaItem` record type.

*   **`opcuaItem` Record Configuration:**
    The `opcuaItem` record acts as a container for an entire structured OPC UA item. It requires an OPC UA subscription.

    ```
    record(opcuaItem, "$(P)STST:OPCUA-ITEM") {
        field(DTYP, "OPCUA")
        field(INP, "@SLOW ns=3;s=\"STST\".\"STRUCT\"")
        field(SCAN, "I/O Intr")
        # Optional: configure write-on-change behavior (WOC)
        # field(WOC, "manual") # default
        # field(WOC, "immediate")
    }
    ```
    *   `@SLOW`: The subscription name.
    *   `ns=3;s=\"STST\".\"STRUCT\""`: Points to the structured OPC UA item.
    *   `SCAN, "I/O Intr"`: Ensures the record processes when the structured data changes.

*   **Linking Element Records to `opcuaItem`:**
    Individual elements within the structured item are accessed by other input/output records.

    *   **INP/OUT Link Field Format for Elements:**
        `@<opcuaItem_record_name> [element=<element_name>] [<option>=<value>...]`

        *   `<opcuaItem_record_name>`: The name of the `opcuaItem` record managing the structured data.
        *   `element=<element_name>`: The path to the specific element within the structure. Use `.` as a hierarchy separator. **No quotation marks or backslashes** for the element path.

    *   **Additional Options for Element Records:**

        | Option     | Value                                         | Description                                    |
        | :--------- | :-------------------------------------------- | :--------------------------------------------- |
        | `element`  | `element name` / `.` / `""`                   | Path to data record's element inside structure |
        | `timestamp`| `server` / `source` / `data` (default: `server`) | Timestamp source.                              |
        | `monitor`  | `y` / `n` (default: `y`)                      | Set up monitor.                                |

    *   **Example: Input Records for Elements of `STST:OPCUA-ITEM`**

        ```
        # Binary Input for a boolean element
        record(bi, "$(P)STST:BOOL") {
            field(DTYP, "OPCUA")
            field(INP, "@$(P)STST:OPCUA-ITEM element=STRUCT.BOOL")
            field(SCAN, "I/O Intr")
            field(ZNAM, "OFF")
            field(ONAM, "ON")
        }

        # Analog Input for a real element
        record(ai, "$(P)STST:REALA") {
            field(DTYP, "OPCUA")
            field(INP, "@$(P)STST:OPCUA-ITEM element=STRUCT.REAL")
            field(SCAN, "I/O Intr")
        }
        ```
        *   `element=STRUCT.BOOL`: Refers to the `BOOL` element inside the `STRUCT` defined in the PLC.

### 2.3 Debugging and Tracing

The OPC UA Device Support module provides several tools for debugging and tracing.

#### 2.3.1 IOC Shell Commands

These commands can be executed directly in the EPICS IOC shell.

| Command             | Description                                                                                                                                                                     |
| :------------------ | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `opcuaOptions`      | Sets options for sessions and subscriptions. Supports glob patterns to set options for multiple sessions/subscriptions.                                                         |
| `opcuaShow`         | Prints information about sessions, subscriptions, items, and their related data elements. Supports glob patterns to match names of things to show.                                |
| `opcuaConnect`      | Manually connects a session.                                                                                                                                                    |
| `opcuaDisconnect`   | Manually disconnects a session.                                                                                                                                                 |
| `opcuaShowSecurity` | Prints information about the security setup of the client or a specific session.                                                                                                |

*   **Example Usage in IOC Shell:**
    ```
    opcuaShow "mySession"
    opcuaShow "fastUpdates"
    help opcuaOptions
    ```

#### 2.3.2 EPICS Record Tracing

You can enable tracing for specific EPICS records.

1.  **Set `TPRO` Field:** Set the `TPRO` field of a record to a value `>= 1` to enable tracing. Higher values increase verbosity.
2.  **Methods:**
    *   **Database file:** Configure `field(TPRO, "1")` in your `.db` file for initialization tracing.
    *   **Live system (IOC Shell):** `caput <PV_NAME>.TPRO 1`
    *   **Remotely:** Using Channel Access or pvAccess client.

#### 2.3.3 Using UaExpert (OPC UA Client)

UaExpert is a powerful OPC UA client tool that can be used to test the connection to your OPC UA server and inspect the available items and their properties (NodeID, attributes, values).

1.  **Download UaExpert:** Obtain UaExpert from the Unified Automation website.
2.  **Connect to your OPC UA Server:**
    *   Add a new server in UaExpert.
    *   Enter the endpoint URL of your PLC's OPC UA server (e.g., `opc.tcp://192.168.2.100:4840`).
    *   Configure security settings to match your server (e.g., `None` for no security).
3.  **Browse the Address Space:** Explore the data blocks and variables exposed by your PLC.
4.  **Inspect NodeIDs:** For each item, check its NodeID attributes:
    *   **NamespaceIndex:** This numerical value is crucial for configuring EPICS records (`ns=<namespace_index>`).
    *   **Identifier:** The string or numerical identifier (`s="identifier"` or `i=identifier`).

    
