#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Growth and health thresholds */
#define REPTILE_GROWTH_MATURE 1.0f       /*!< Growth value at maturity          */
#define REPTILE_HEALTH_SICK_RATIO 0.3f   /*!< Fraction of max health for illness*/
#define REPTILE_HEALTH_DEAD 0.0f         /*!< Health level indicating death     */

/* CITES appendix classification */
typedef enum {
    REPTILE_CITES_NONE = 0, /*!< Species not listed in CITES */
    REPTILE_CITES_I,        /*!< Appendix I */
    REPTILE_CITES_II,       /*!< Appendix II */
    REPTILE_CITES_III       /*!< Appendix III */
} reptile_cites_t;

/* Geographic regions used for legality checks */
typedef enum {
    REPTILE_REGION_FR = 0, /*!< France */
    REPTILE_REGION_EU,     /*!< European Union */
    REPTILE_REGION_INTL    /*!< Outside the EU */
} reptile_region_t;

typedef struct {
    float temperature;        /*!< Optimal basking temperature (°C)      */
    float humidity;           /*!< Target humidity (%)                    */
    float uv_index;           /*!< UV index requirement                   */
    float terrarium_min_size; /*!< Minimum terrarium size (m^2)           */
    float growth_rate;        /*!< Growth rate per tick                   */
    float max_health;         /*!< Maximum health value                   */
} reptile_needs_t;

/* Regulatory constraints for a species */
typedef struct {
    reptile_cites_t cites;      /*!< CITES appendix classification          */
    bool requires_authorisation; /*!< Prefectoral authorisation required     */
    bool requires_cdc;          /*!< Certificat de capacité (CDC) required  */
    bool requires_certificat;   /*!< Additional certificate requirement     */
    bool requires_declaration;  /*!< Préfecture declaration mandatory       */
    bool requires_marking;      /*!< Identification/marking mandatory       */
    bool dangerous;             /*!< Species classified as dangerous        */
    uint16_t max_without_permit;/*!< Max specimens without CDC/APD          */
    uint16_t max_total;         /*!< Absolute cap on specimens              */
    bool allowed_fr;            /*!< Species permitted in France            */
    bool allowed_eu;            /*!< Species permitted in EU                */
    bool allowed_international; /*!< Species permitted internationally      */
} reptile_legal_t;

/* Player context: permits owned and current location */
typedef struct {
    reptile_cites_t cites_permit; /*!< Highest CITES appendix permitted  */
    bool has_authorisation;       /*!< Holds prefectoral authorisation   */
    bool has_cdc;                 /*!< Holds certificat de capacité      */
    bool has_certificat;          /*!< Holds additional certificate      */
    bool has_declaration;         /*!< Prefectoral declaration filed     */
    bool has_marking_system;      /*!< Identification/marking compliant  */
    bool has_dangerous_permit;    /*!< Permit for dangerous species      */
    uint16_t declared_specimens;  /*!< Number of specimens declared      */
    reptile_region_t region;      /*!< Current geographic region         */
} reptile_user_ctx_t;

typedef struct {
    const char *species;  /*!< Scientific name */
    reptile_needs_t needs;/*!< Biological requirements */
    reptile_legal_t legal;/*!< Regulatory data */
} reptile_info_t;

bool reptiles_load(void);
const reptile_info_t *reptiles_get(size_t *count);
const reptile_info_t *reptiles_find(const char *species);

/* Compare legal requirements with player's context */
bool reptiles_check_compliance(const reptile_legal_t *legal,
                               const reptile_user_ctx_t *ctx);
/* Validate biological needs and legal compliance */
bool reptiles_validate(const reptile_info_t *info,
                       const reptile_user_ctx_t *ctx);
/* Add a reptile after validation */
bool reptiles_add(const reptile_info_t *info,
                  const reptile_user_ctx_t *ctx);
