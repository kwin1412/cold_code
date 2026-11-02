#ifndef MODULE_H
#define MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Add two integers.
 *
 * @param lhs Left operand.
 * @param rhs Right operand.
 * @return The sum of lhs and rhs.
 */
int module_add(int lhs, int rhs);

/**
 * Return the module version string.
 *
 * Keep this in sync with the project version declared in the top-level
 * CMakeLists.txt.
 */
const char *module_version(void);

#ifdef __cplusplus
}
#endif

#endif /* MODULE_H */
