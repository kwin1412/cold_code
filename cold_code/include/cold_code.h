#ifndef COLD_CODE_H
#define COLD_CODE_H

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
int cold_code_add(int lhs, int rhs);

/**
 * Return the cold_code version string.
 *
 * Keep this in sync with the project version declared in the top-level
 * CMakeLists.txt.
 */
const char *cold_code_version(void);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_H */
