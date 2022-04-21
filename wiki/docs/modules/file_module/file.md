# File

## Static methods

### exists(path)

Returns `true` if there is a file present at the given path.

### getLastModified(path)

Returns the time (in unix timestamp) of the last modification of the file.

## Instance fields

### exists

Equals to `true` if the file exists on disk.

## Instance methods

### constructor(path)

Creates a new file instance with the path set to `path`. This method does not create the file on disk, it just acts as a handle.

### close()

Closes the file, writing it to the disk. Creates the file on the disk, if it did not exist before.

### write(object)

Writes the result of calling `object.toString()` to the file buffer.

### writeByte(byte)

Writes a byte to the file buffer.

### writeShort(short)

Writes two bytes to the file buffer.

### writeNumber(number)

Writes four bytes to the file buffer.

### writeBool(bool)

Writes either `1` or `0` to the file buffer depending on the provided value.

### writeString(string)

Writes a string with a maximum length in bytes of ~32k chars (`INT16_MAX`) to the file buffer.
It uses two bytes to store the string's byte length, and then dumps all the string's bytes.

### readAll()

Returns the file contents as a string.

### readLine()

Returns a single line from the file as a string and advances the file position.

### readByte()

Returns a single byte from the file and advances the file position.

### readShort()

Returns two bytes from the file and advances the file position.

### readNumber()

Returns four bytes from the file and advances the file position.

### readBool()

Returns `true` or `false` from the file and advances the file position.

### readString()

Returns a string from the file and advances the file position.
Be careful to use this method only with the correct byte structure in the file, aka written by the `writeString()`.

### getLastModified()

Returns the time (in unix timestamp) of the last modification of the file.