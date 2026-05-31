# iocsh Command Reference

```{versionadded} 0.9.0
The unified `opcuaSession`, `opcuaSubscription`, and `opcuaOptions` commands were introduced in version 0.9.0, replacing the older `opcuaCreate...` commands.
```

## Session Management

### `opcuaSession`
Configures a new OPC UA session.
```
opcuaSession <name>, <URL>, [<options>]
```
*   `name`: Unique name for the session.
*   `URL`: Server endpoint (e.g., `opc.tcp://localhost:4840`).
*   `options`: Key-value pairs (e.g., `AutoConnect=YES`).

### `opcuaConnect` / `opcuaDisconnect`
Manually connect or disconnect sessions matching a glob pattern.

### `opcuaMapNamespace`
```{versionadded} 0.8.0
```
Maps a numerical namespace index used in the database to a URI on the server.

## Subscription Management

### `opcuaSubscription`
Configures a new subscription under an existing session.
```
opcuaSubscription <name>, <session>, <publishing interval [ms]>, [<options>]
```

## Security Management

### `opcuaClientCertificate`
Sets the client's own certificate and private key.

### `opcuaSetupPKI`
Sets the location of the PKI certificate store.

### `opcuaSaveRejected`
Sets the location where rejected server certificates are saved.

### `opcuaShowSecurity`
Shows discovered endpoints and security details for a session.

## Inspection and Debugging

### `opcuaShow`
Shows details for sessions, subscriptions, or records.
```
opcuaShow <pattern>, [<verbosity>]
```

### `opcuaOptions`
Sets options for existing sessions or subscriptions matching a pattern.
