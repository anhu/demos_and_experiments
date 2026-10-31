/* phrases.h -- the text typed by image 4 (04_click_phrases).
 *
 * Phrase N is typed after N clicks of the BOOTSEL button. Edit the strings,
 * rebuild, reflash. Rules:
 *   - plain ASCII only; characters outside the US-layout table are skipped;
 *   - trailing spaces are intentional and ARE typed;
 *   - no Enter is sent after a phrase (add "\n" to a string if you want one);
 *   - the host must be on a US keyboard layout for punctuation to come out right;
 *   - never put passwords or other secrets here (project rule).
 */
#ifndef PHRASES_H_
#define PHRASES_H_

#define PHRASE_COUNT 5

static const char *const PHRASES[PHRASE_COUNT] = {
    "Insert message for 1 click here",
    "Insert message for 2 clicks here",
    "Insert message for 3 clicks here",
    "Insert message for 4 clicks here",
    "Insert message for 5 clicks here",
};

#endif /* PHRASES_H_ */
