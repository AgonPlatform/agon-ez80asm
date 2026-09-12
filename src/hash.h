#ifndef HASH_H
#define HASH_H

//uint24_t hash(char *key); // returns hash as 16bit unsigned int
uint8_t hash256(const char *key);
uint8_t lowercaseHash256(const char *key);

#endif // HASH_H