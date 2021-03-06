from intelhex import IntelHex
import hashlib
import os


def generateFlashAndEepromHex(originalFlashHexName, bootloaderHexName, serialNumber, AESKey1, AESKey2, UIDKey, UID, newFlashHexName, newEeepromHex, verbose):
	FW_MAX_LENGTH = 28672
	BL_MAX_LENGTH = 4096
	AES_KEY_LENGTH = 256/8
	AES_BLOCK_LENGTH = 128/8
	UID_REQUEST_KEY_LENGTH = 16
	UID_KEY_LENGTH = 6
	
	# Merged bootloader and firmware sha1 hash
	merged_fw_bl_sha1_hash = ""
	
	# Check for original firmware file presence
	if not os.path.isfile(originalFlashHexName):
		print "Couldn't find firmware hex file", originalFlashHexName
		return [False, merged_fw_bl_sha1_hash]
				
	# Check for bootloader file presence
	if not os.path.isfile(bootloaderHexName):
		print "Couldn't find bootloader hex file", bootloaderHexName
		return [False, merged_fw_bl_sha1_hash]
		
	# Check AES Key Length
	if len(AESKey1) != AES_KEY_LENGTH:
		print "Wrong AES Key1 length!"
		return [False, merged_fw_bl_sha1_hash]
		
	# Check AES Key Length
	if len(AESKey2) != AES_KEY_LENGTH:
		print "Wrong AES Key2 length!"
		return [False, merged_fw_bl_sha1_hash]
		
	# Check UID Req Key Length
	if len(UIDKey) != UID_REQUEST_KEY_LENGTH:
		print "Wrong UID request key length!"
		return [False, merged_fw_bl_sha1_hash]
		
	# Check UID Key Length
	if len(UID) != UID_KEY_LENGTH:
		print "Wrong UID key length!"
		return [False, merged_fw_bl_sha1_hash]
		
	# Read firmware Hex
	flashHex = IntelHex(originalFlashHexName)
	if len(flashHex) > FW_MAX_LENGTH:
		print "Firmware file too long:", len(flashHex), "bytes long"
		return [False, merged_fw_bl_sha1_hash]
	else:	
		if verbose == True:
			print "Firmware file is", len(flashHex), "bytes long"
			
	# Get fw version number
	firmware_bin = flashHex.tobinarray()
	for i in range(len(firmware_bin) - 3):
		if chr(firmware_bin[i]) == 'v' and \
		chr(firmware_bin[i + 1]) >= '1' and chr(firmware_bin[i + 1]) <= '9' and \
		chr(firmware_bin[i + 2]) == '.' and \
		chr(firmware_bin[i + 3]) >= '0' and chr(firmware_bin[i + 3]) <= '9':
			firmware_version = firmware_bin[i:i+4]
			if verbose == True:
				print "Extracted firmware version:", "".join(chr(firmware_version[j]) for j in range(0, 4))
			break;
			
	# Check there's nothing where we want to put the fw version number
	if flashHex[FW_MAX_LENGTH-4] != 0xFF:
		print "No space to write fw version number inside the firmware hex!"
		return [False, merged_fw_bl_sha1_hash]
		
	# Write the fw version number in the last 4 bytes of the firmware hex
	flashHex[FW_MAX_LENGTH-4] = firmware_version[0]
	flashHex[FW_MAX_LENGTH-3] = firmware_version[1]
	flashHex[FW_MAX_LENGTH-2] = firmware_version[2]
	flashHex[FW_MAX_LENGTH-1] = firmware_version[3]
	
	# Read bootloader hex
	bootloaderHex = IntelHex(bootloaderHexName)
	if len(bootloaderHex) > BL_MAX_LENGTH:
		print "Bootloader file too long:", len(bootloaderHex), "bytes long"
		return [False, merged_fw_bl_sha1_hash]
	else:	
		if verbose == True:
			print "Bootloader file is", len(bootloaderHex), "bytes long"
	
	# Merge firmware with bootloader	
	flashHex.merge(bootloaderHex)
	
	# Generate hash, print it if need
	merged_fw_bl_sha1_hash = hashlib.sha1(flashHex.tobinarray()).hexdigest()
	if verbose == True:
		print "Original Firmware/Bootloader Hash:", merged_fw_bl_sha1_hash
		
	# Check there's nothing where we want to put the serial number
	if flashHex[0x7F7C] != 0xFF:
		print "No space to write serial number inside the bootloader hex!"
		return [False, merged_fw_bl_sha1_hash]

	# Include serial number in the hex to be flashed
	flashHex[0x7F7C] = (serialNumber >> 24) & 0x000000FF
	flashHex[0x7F7D] = (serialNumber >> 16) & 0x000000FF
	flashHex[0x7F7E] = (serialNumber >> 8) & 0x000000FF
	flashHex[0x7F7F] = (serialNumber >> 0) & 0x000000FF
	
	# Write production firmware file
	flashHex.tofile(newFlashHexName, format="hex")
	
	# Generate blank eeprom file
	eepromHex = IntelHex()
	for i in range(0x400):
		eepromHex[i] = 0xFF
		
	# Modify the byte in eeprom to indicate normal boot
	# Address 0: 0xDEAD, little endian
	eepromHex[0] = 0xAD
	eepromHex[1] = 0xDE
	# Address 2: 0xAB (bootloader password set)
	eepromHex[2] = 0xAB
	# Address 98: mass production boolean 
	eepromHex[98] = 0xCD
	# Address 999: boolean specifiying that uid key is set
	eepromHex[999] = 0xBB
	# Address 1000: UID request key
	eepromHex[1000:1000+UID_REQUEST_KEY_LENGTH] = UIDKey
	# Address 1016: UID key
	eepromHex[1000+UID_REQUEST_KEY_LENGTH:1000+UID_REQUEST_KEY_LENGTH+UID_KEY_LENGTH] = UID
	# Address 0x03: 32 bytes of AES key 1 + 30 first bytes of AES key 2
	eepromHex[3:3+AES_KEY_LENGTH] = AESKey1
	eepromHex[3+AES_KEY_LENGTH:3+AES_KEY_LENGTH+30] = AESKey2[0:30]
	# Last 2 bytes in EEPROM: last 2 AESKey 2 bytes
	eepromHex[0x400-2] = AESKey2[30]
	eepromHex[0x400-1] = AESKey2[31]
	
	# Write new eeprom file
	eepromHex.tofile(newEeepromHex, format="hex")
	
	# Return success and hash
	return [True, merged_fw_bl_sha1_hash]