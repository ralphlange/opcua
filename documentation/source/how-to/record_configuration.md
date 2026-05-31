# Configuring EPICS Records for OPC UA

This guide shows how to configure EPICS records for various OPC UA data exchange scenarios.

## Single Value Data Exchanges

For individual OPC UA items mapped to single EPICS records.

### General INP/OUT Link Field Format

`@<session_name> ns=<namespace_index>;<identifier_type>=<identifier> [<option>=<value>...]`

*   `<session_name>`: Name of the OPC UA session.
*   `<namespace_index>`: Numerical namespace index.
*   `<identifier_type>`: `s` for string identifier, `i` for numerical identifier.
*   `<identifier>`: The string or numerical ID of the OPC UA node. **Escape quotation marks** (`\"`) if the identifier string contains them (e.g., `"dataBlock"."myItem"` becomes `\"dataBlock\".\"myItem\"`).

### Example: Config Unmonitored (CFGU) Variables (Output Records)

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

### Example: Config Monitored (CFGM) Variables (Output Records with Readback)

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

### Example: State Single Variables (STSV) (Input Records)

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

## Structured Data Exchanges

```{versionadded} 0.3.0
The `opcuaItem` record type for structured data exchange was added in version 0.3.0.
```

For exchanging entire data structures (blocks) between the IOC and OPC UA server using the `opcuaItem` record type.

### `opcuaItem` Record Configuration

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

### Linking Element Records to `opcuaItem`

Individual elements within the structured item are accessed by other input/output records.

#### INP/OUT Link Field Format for Elements

`@<opcuaItem_record_name> [element=<element_name>] [<option>=<value>...]`

*   `<opcuaItem_record_name>`: The name of the `opcuaItem` record managing the structured data.
*   `element=<element_name>`: The path to the specific element within the structure. Use `.` as a hierarchy separator. **No quotation marks or backslashes** for the element path.

#### Example: Input Records for Elements of `STST:OPCUA-ITEM`

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
