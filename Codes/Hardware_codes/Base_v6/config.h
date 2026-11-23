#ifndef CONFIG_H
  #define CONFIG_H
  #define SECRET_KEY 0x5A89C3F1 // Libovolný unikátní klíč (musí být stejný na vysílači i základně)
  // MAC address of the receiver (replace with your target device's MAC address)
  uint8_t receiverMAC[] = {0xd0, 0xef, 0x76, 0x44, 0xe3, 0x98}; // Replace with actual MAC address
  uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#endif // CONFIG_H  