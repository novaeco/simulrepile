#ifndef COMPLIANCE_H
#define COMPLIANCE_H

#include <stdbool.h>

typedef enum {
  COMPLIANCE_TOPIC_TERRARIUM_SIZE = 0,
  COMPLIANCE_TOPIC_CERTIFICATE,
  COMPLIANCE_TOPIC_PROTECTED_SPECIES,
  COMPLIANCE_TOPIC_COUNT
} compliance_topic_t;

void compliance_show_quiz(compliance_topic_t topic);
bool compliance_is_active(void);
void compliance_dismiss(void);
const char *compliance_topic_reference(compliance_topic_t topic);

#endif /* COMPLIANCE_H */
