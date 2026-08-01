# GATT Server

Read/Write可能なCharacteristicとDescriptorを持つ独自Serviceを公開します。

GATT databaseは `begin()` で確定するため、`addService()`、`addCharacteristic()`、`addDescriptor()` はすべて `begin()` より前に呼びます。登録後の操作では、UUIDではなく返されたopaque handleを使います。
