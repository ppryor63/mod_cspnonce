/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * mod_cspnonce.c: Generate a cryptographically secure base64 encoded CSP nonce.
 *
 * Original author: wyDay, LLC <support@wyday.com>
 *
 * https://github.com/wyday/mod_cspnonce
*/

#include "apr_base64.h"

#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h" /* for ap_hook_post_read_request */


#ifdef _WIN32
#    include <Windows.h>
#    include <bcrypt.h>
#    include <stdio.h>

#    pragma comment(lib, "Bcrypt")
#else
#    include <stdlib.h>
#    ifndef __APPLE__
#        include <time.h>
#    endif
#endif

typedef unsigned char byte;

/*
* Generates a 12-character string (13 bytes to account for null).
* It's random and base64 encoded.
*
* On error NULL is returned.
*/
const char * GenSecureCSPNonce(const request_rec * r)
{
    // Generate 18 random bytes (144-bits). Any multiple of 3 will work
    // well because the base64 string generated will not require
    // padding (i.e. useless characters).
    // If you modify this number you'll need to modify the string length
    // and null terminator below.
    byte random_bytes[18];

    // This number is based on the seemlingly made-up number used in
    // the W3C "webappsec-csp" document. It seems made-up (i.e. a number
    // not based on either theoretical or real-world testing) because
    // 128-bits cannot be divided evenly into a base64-encoded string.
    // But, whatever, I'll let someone else fight that battle.
    // Here is the nonsense source: https://w3c.github.io/webappsec-csp/#security-nonces

#ifdef _WIN32
    BCRYPT_ALG_HANDLE Prov;

    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&Prov, BCRYPT_RNG_ALGORITHM, NULL, 0)))
    {
        return NULL;
    }

    if (!BCRYPT_SUCCESS(BCryptGenRandom(Prov, (PUCHAR)(random_bytes), sizeof(random_bytes), 0)))
    {
        BCryptCloseAlgorithmProvider(Prov, 0);
        return NULL;
    }

    BCryptCloseAlgorithmProvider(Prov, 0);

#else  // POSIX

    // This assumes that posix uses a secure PRNG
    // on the system. This may or may not be true
    // depending on the system. With modern kernels this
    // will be true.
    // https://man7.org/linux/man-pages/man3/random.3.html
    int h;

// Seed the PRNG
#    ifdef __APPLE__
    srandomdev();
#    else
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) == 0)
        return NULL;

    srandom(ts.tv_nsec ^ ts.tv_sec);
#    endif

    // Generate a random integer
    // fill up bytes 0,1,2,3
    h = random();
    memcpy(random_bytes, &h, 4);

    // fill up bytes 4,5,6,7
    h = random();
    memcpy(random_bytes + 4, &h, 4);

    // fill up bytes 8,9,10,11
    h = random();
    memcpy(random_bytes + 8, &h, 4);

    // fill up bytes 12,13,14,15
    h = random();
    memcpy(random_bytes + 12, &h, 4);

    // fill up bytes 14,15,16,17
    // Yes, there's overlap.
    h = random();
    memcpy(random_bytes + 14, &h, 4);

#endif

    char * cspNonce;

    // Allocate 25 bytes for the base64 string + NULL.
    // Base64 uses 4 ascii characters to encode 24-bits (3 bytes) of data
    // Thus we need 24 characters + 1 NULL char to store 18 bytes of random data.
    cspNonce = (char *)apr_palloc(r->pool, 25);

    // null terminate string
    cspNonce[24] = '\0';

    apr_base64_encode(cspNonce, (const char *)random_bytes, sizeof(random_bytes));

    return cspNonce;
}

static int set_cspnonce(request_rec * r)
{
    const char * id = NULL;

    /* copy the CSP_NONCE if this is an internal redirect (we're never
     * actually called for sub requests, so we don't need to test for
     * them) */
    if (r->prev)
        id = apr_table_get(r->subprocess_env, "REDIRECT_CSP_NONCE");

    if (id == NULL)
        id = GenSecureCSPNonce(r);

    /* set the environment variable */
    if (id != NULL)
        apr_table_setn(r->subprocess_env, "CSP_NONCE", id);

    return DECLINED;
}

static void register_hooks(apr_pool_t * p)
{
    ap_hook_post_read_request(set_cspnonce, NULL, NULL, APR_HOOK_MIDDLE);
}

#ifdef AP_DECLARE_MODULE
AP_DECLARE_MODULE(cspnonce) = {
    STANDARD20_MODULE_STUFF,
    NULL,          /* dir config creater */
    NULL,          /* dir merger --- default is to override */
    NULL,          /* server config */
    NULL,          /* merge server configs */
    NULL,          /* command apr_table_t */
    register_hooks /* register hooks */
};
#else
module AP_MODULE_DECLARE_DATA cspnonce_module = {
    STANDARD20_MODULE_STUFF,
    NULL,          /* dir config creater */
    NULL,          /* dir merger --- default is to override */
    NULL,          /* server config */
    NULL,          /* merge server configs */
    NULL,          /* command apr_table_t */
    register_hooks /* register hooks */
};
#endif
