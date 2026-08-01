# GATT Server

Publishes a custom service with a readable/writable characteristic and descriptor.

Register the complete database before `begin()`. Later operations use the opaque handles returned by the registration calls instead of UUIDs.
