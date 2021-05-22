## wsrep API usage examples

### 1. Listener
Is a simple program that connects and listens to replication events in
an existing cluster.

Usage example (starting listener on the same host as the rest of the cluster):
```
$ ./listener /path_to/libgalera_smm.so gcomm://localhost:4567?gmcast.listen_addr=tcp://127.0.0.1:9999 cluster_name
```

### 2. Node
Is a more complex program which implements most of wsrep node functionality
and can form clusters in itself.
