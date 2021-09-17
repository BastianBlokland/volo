#include "core_file.h"

#include "output_internal.h"

/**
 * Mocha json reporter output.
 *
 * Mocha is a popular JavaScript unit testing library (https://github.com/mochajs/mocha) and the
 * json reporter format is supported by various tools.
 *
 * Example output:
 * ```
 * {
 *   "stats": {
 *     "start": "2021-09-09T14:36:45.947Z",
 *     "end": "2021-09-09T14:36:45.951Z",
 *     "duration": 4
 *     "tests": 1,
 *     "passes": 1,
 *     "failures": 0,
 *     "pending": 0,
 *   },
 *   "passing": {
 *       "title": "returns FizzBuzz for multiples of 3 and 5",
 *       "fullTitle": "fizzbuzz returns FizzBuzz for multiples of 3 and 5",
 *       "file": "libs/check/test/test_fizzbuzz.c",
 *       "duration": 1,
 *       "err": {}
 *     }
 *   ],
 *   "failures": [],
 *   "pending": []
 * }
 * ```
 * Note: Durations are in whole milliseconds.
 * Note: Skipped tests are categorized as 'pending' in the Mocha json format.
 *
 * Aims for compatiblity with the Mocha json reporter from v7.2.0 or higher.
 */

/**
 * Create a mocha json output that writes to the given file.
 * Note: the given file handle is automatically destroyed when the output is destroyed.
 * Note: Destroy using 'check_output_destroy()'.
 */
CheckOutput* check_output_mocha(Allocator*, File*);

/**
 * Create a mocha json output that writes a file at the given path.
 */
CheckOutput* check_output_mocha_to_path(Allocator*, String path);

/**
 * Create a mocha json output that writes a file called '[executable-name]_[timestamp].mocha' in a
 * directory called 'logs' next to the executable.
 */
CheckOutput* check_output_mocha_default(Allocator*);
