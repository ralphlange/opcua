# OPC UA Security Concepts

The security features of OPC UA are based on X.509 certificates and Public Key Infrastructure (PKI).

## Communication Security

Each OPC UA application (server and client) needs an **Application Instance Certificate** to identify itself.

### Message Security Modes
*   **None**:
    No security.
*   **Sign**:
    Messages are signed for integrity.
*   **SignAndEncrypt**:
    Messages are signed and encrypted for both integrity and confidentiality.

### Security Policies
Security policies define the algorithms used for signing and encryption.
Common policies include:
*   `Basic256Sha256`
*   `Aes128_Sha256_RsaOaep`
*   `Aes256_Sha256_RsaPss`

## Client Authentication (Identity)

Beyond securing the communication channel, servers often require the client to identify as a specific user.

*   **Anonymous**:
    No user information.
*   **Username**:
    Identification via name and password.
*   **X.509 Certificate**:
    Identification via a specific user certificate.

## Endpoint Discovery

Clients discover a server's security requirements by querying its **Discovery Endpoint**.
The server returns a list of available endpoints, each with its own security mode, policy, and required user token types.

The EPICS IOC will attempt to choose the most secure endpoint that matches its configuration.
