# EPICS Records for Structured Data

:::{versionadded} 0.3
The `opcuaItem` record type and support for structured data.
:::

This guide shows how to configure EPICS records
for accessing structured data on the OPC UA server.
This is achieved using the `opcuaItem` record type
in combination with regular EPICS records.

The `opcuaItem` record connects to the Item
that represents the structure.
Leaf elements are accessed through regular EPICS records,
one for each element that is interfaced.

## Creating the Root `opcuaItem`

### INP Link Field Format for the `opcuaItem` Record

This format is the same as for the INP/OUT links
of single scalar variables.

`@<session_name> ns=<namespace_index>;<identifier_type>=<identifier>
[<option>=<value>...]`

* `<session_name>`:
  Name of the OPC UA Session.
* `<namespace_index>`:
  Numerical namespace index.
* `<identifier_type>`:
  `s` for string identifier, `i` for numerical identifier.
* `<identifier>`:
  The string or numerical ID of the OPC UA Node.

  :::{note}
  **Escape quotation marks** (`\"`)
  if the identifier string contains them
  (e.g., `"dataBlock"."myItem"` becomes `\"dataBlock\".\"myItem\"`).
  :::

### Available Options

:::{list-table}
:header-rows: 1

* - Option
  - Default
  - Description
* - `sampling`
  - `-1`
  - Sampling interval in ms [double; -1 = use publishing interval]
* - `qsize`
  - `1`
  - Server-side queue size [int; 1 = no queueing]
* - `cqsize`
  - `1.5 * qsize`
  - Client-side queue size [int; minimum 3]
* - `discard`
  - `old`
  - Discard policy on queue overrun: [`old`/`new` (drop oldest/drop newest)]
* - `register`
  - `n`
  - Register item with server for performance [`y`/`n`]
* - `update`
  - `change`
  - Default for the element records (which can override it):
    which monitor updates are passed to them:
    `change`/`chg` (only changes of value or status), `always`/`alw` (all);
    see [Updating Only Changed Elements](#updating-only-changed-elements)
* - `bini`
  - `read`
  - Behavior at init: `read`, `ignore`, `write`
* - `monitor`
  - `y`
  - Enable monitoring. For outputs, enables readback
* - `timestamp`
  - `server`
  - Source of timestamp: `server`/`source`/`@<top_element>`
    [`top_element` points to source of timestamp]
:::

### `opcuaItem` Record Configuration

The `opcuaItem` record connects to a structured variable (Item)
on the OPC UA server.
It requires an OPC UA subscription.

The `opcuaItem` record is bidirectional.
Explicit `read` and `write` operations can be triggered
by writing to the (pseudo) fields `READ` or `WRITE`.

```
record(opcuaItem, "$(P)FOO:OPCUA-ITEM") {
    field(DTYP, "OPCUA")
    field(INP, "@SLOW ns=3;s=\"DB1\".\"FOO\"")
    field(SCAN, "I/O Intr")
    field(DEFACTN, "read")
    field(WOC, "immediate")
}
```

* `@SLOW`:
  The subscription name.
* `ns=3;s=\"DB1\".\"FOO\""`:
  Points to the structured OPC UA Item.
* `SCAN, "I/O Intr"`:
  Ensures the record processes when the structured data changes.
* `DEFACTN` specifies the default action
  when the record is processed: `read` or `write`.
* `WOC` controls the writing behavior
  when single elements of the structure are changed.
  `manual` (default)
  requires explicit processing of the record to write the structure.
  `immediate`
  processes the record (writes the structure)
  as soon as an element changes.
  This can be used to avoid writing inconsistent data
  while multiple elements of the structure are written in a transactional fashion,
  e.g., when restoring a snapshot or loading a configuration.

## Linking Element Records to `opcuaItem`

Individual elements within the structured item
are accessed by other regular input/output records.

### INP/OUT Link Field Format for Elements

`@<opcuaItem_record_name> [element=<element_name>] [<option>=<value>...]`

* `<opcuaItem_record_name>`:
  The name of the `opcuaItem` record managing the structured data.
* `element=<element_name>`:
  The path to the specific element within the structure.
  Use `.` as a hierarchy separator.

### Available Options

:::{list-table}
:header-rows: 1

* - Option
  - Default
  - Description
* - `element`
  - 
  - Path to element inside an `opcuaItem` structure
    [`.` = hierarchy separator; `<empty>` = root element if no structure]
* - `bini`
  - `read`
  - Behavior at init: `read`/`ignore`/`write`
* - `monitor`
  - `y`
  - Enable monitoring. For outputs, enables readback
* - `update`
  - setting of the `opcuaItem` record
  - Which monitor updates process the record:
    `change`/`chg` (only changes of value or status), `always`/`alw` (all);
    see [Updating Only Changed Elements](#updating-only-changed-elements)
* - `timestamp`
  - `server`
  - Source of timestamp: `server`/`source`/`data`
:::

:::{note}
To allow the Device Support to set the timestamp of a record,
the record's `TSE` field must be set to `-2`.
:::

### Example: Input Records for Elements of `$(P)FOO:OPCUA-ITEM`

```
# Binary Input for a boolean element
record(bi, "$(P)FOO:BOOL") {
    field(DTYP, "OPCUA")
    field(INP, "@$(P)FOO:OPCUA-ITEM element=STRUCT.BOOL")
    field(SCAN, "I/O Intr")
    field(ZNAM, "OFF")
    field(ONAM, "ON")
}

# Analog Input for a real element
record(ai, "$(P)FOO:REALA") {
    field(DTYP, "OPCUA")
    field(INP, "@$(P)FOO:OPCUA-ITEM element=STRUCT.REAL")
    field(SCAN, "I/O Intr")
}
```

* `element=STRUCT.BOOL`:
  Refers to the `BOOL` sub-element inside the `STRUCT` element
  of the structure that the `opcuaItem` record points to.

(updating-only-changed-elements)=
## Updating Only Changed Elements

:::{versionadded} 0.12
The `update` option.
:::

The OPC UA server sends the complete structure
whenever any of its members changes.
Processing every element record for every update of the structure,
even if the value of its element has not changed,
would waste most of the work for large structures,
where only a small part of the elements changes between updates
(e.g., the state of a PLC with thousands of boolean flags):
the IOC would spend most of its time processing records
that do not change.

Therefore, by default (`update=change` or `update=chg`),
an element record is only processed for a monitor update
if the value of its element or the status of the item has changed
since the last monitor update that was passed to it.

With `update=always` (or `update=alw`)
in its link,
an element record is processed for every monitor update of the structure.

:::{note}
With the open62541 client library before version 1.3,
the `update` option has no effect:
element records are processed for every monitor update
(as with `update=always`).
:::

The `update` option only applies to elements of structures
and to data updates from the monitored item (subscription).
It is ignored for records that access a single scalar variable,
as the OPC UA monitor only sends their data when the value (or status) has changed.
Data from explicit reads
(e.g., `bini=read` or processing a non-monitored input record)
always processes the records.

Set in the link of the `opcuaItem` record,
the `update` option is the default for all its element records.
Element records can override it with their own `update` option.
The `opcuaItem` record itself is still processed for every update.

```
# Element records only process on changes (default)...
record(opcuaItem, "$(P)PLC:STATE") {
    field(DTYP, "OPCUA")
    field(INP, "@SLOW ns=3;s=\"DB1\".\"STATE\"")
    field(SCAN, "I/O Intr")
}

record(bi, "$(P)PLC:VALVE1:OPEN") {
    field(DTYP, "OPCUA")
    field(INP, "@$(P)PLC:STATE element=VALVE1.OPEN")
    field(SCAN, "I/O Intr")
}

# ...except this one, which is processed for every update
record(longin, "$(P)PLC:HEARTBEAT") {
    field(DTYP, "OPCUA")
    field(INP, "@$(P)PLC:STATE element=HEARTBEAT update=always")
    field(SCAN, "I/O Intr")
    field(FLNK, "$(P)PLC:WATCHDOG")
}
```

The following updates are always passed to the element records:

* the data of explicit reads (including `bini=read`)
  and the first monitor update after (re)connecting or after a read,
* updates after a connection loss,
* the first update after the record has written to its element
  (so that the readback shows the server's value).

:::{caution}
Unchanged element records are not processed, so:

* their timestamp (`TIME` field) is not updated,
  i.e., it is the time of the last change,
  not the time of the last update of the structure.
  This also applies to timestamps taken from the data (`timestamp=@<element>`);
* records linked through their forward link (`FLNK`)
  are only processed when the value changes.

Use `update=always`
for records that need to be processed
for every update of the structure.
:::

:::{tip}
`opcuaShow <subscription> 1` shows the timing of the data change callbacks
(the time spent pushing incoming data into the element records)
and the item options, including `update`;
`opcuaShow <subscription> 2` adds the options of the element records.
:::
