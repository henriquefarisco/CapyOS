#ifndef KEYBOARD_COMPOSE_H
#define KEYBOARD_COMPOSE_H

static inline int keyboard_compose_is_dead_accent(char ch) {
  return ch == '\'' || ch == '`' || ch == '^' || ch == '~' || ch == '"';
}

static inline char keyboard_compose_dead_key(char accent, char base) {
  if (accent == '\'') {
    if (base == 'a')
      return (char)0xA0;
    if (base == 'e')
      return (char)0x82;
    if (base == 'i')
      return (char)0xA1;
    if (base == 'o')
      return (char)0xA2;
    if (base == 'u')
      return (char)0xA3;
    if (base == 'A')
      return (char)0xB5;
    if (base == 'E')
      return (char)0x90;
    if (base == 'I')
      return (char)0xD6;
    if (base == 'O')
      return (char)0xE0;
    if (base == 'U')
      return (char)0xE9;
  } else if (accent == '^') {
    if (base == 'a')
      return (char)0x83;
    if (base == 'e')
      return (char)0x88;
    if (base == 'i')
      return (char)0x8C;
    if (base == 'o')
      return (char)0x93;
    if (base == 'u')
      return (char)0x96;
  } else if (accent == '~') {
    if (base == 'a')
      return (char)0xC6;
    if (base == 'o')
      return (char)0xE5;
    if (base == 'A')
      return (char)0xC7;
    if (base == 'O')
      return (char)0xE4;
  } else if (accent == '`') {
    if (base == 'a')
      return (char)0x85;
    if (base == 'A')
      return (char)0xB7;
  } else if (accent == '"') {
    if (base == 'u')
      return (char)0x81;
    if (base == 'U')
      return (char)0x9A;
  }
  return 0;
}

/*
 * Consume one printable key through the dead-key state machine.
 *
 * Returns 1 when a character should be emitted immediately in out_char.
 * Returns 0 when the key only armed a dead accent and no output is ready yet.
 *
 * If a non-composable character follows a dead accent, out_char receives the
 * accent and pending_char receives the base character so the caller can emit it
 * on the next poll/step.
 */
static inline int keyboard_compose_step(char *dead_accent, char *pending_char,
                                        char ch, int is_dead, char *out_char) {
  if (!dead_accent || !pending_char || !out_char) {
    return 0;
  }

  if (*dead_accent) {
    char accent = *dead_accent;
    *dead_accent = 0;

    if (ch == ' ') {
      *out_char = accent;
      return 1;
    }

    if (is_dead && keyboard_compose_is_dead_accent(ch)) {
      *out_char = accent;
      if (ch != accent) {
        *dead_accent = ch;
      }
      return 1;
    }

    {
      char composed = keyboard_compose_dead_key(accent, ch);
      if (composed) {
        *out_char = composed;
        return 1;
      }
    }

    *out_char = accent;
    *pending_char = ch;
    return 1;
  }

  if (is_dead && keyboard_compose_is_dead_accent(ch)) {
    *dead_accent = ch;
    return 0;
  }

  *out_char = ch;
  return 1;
}

#endif /* KEYBOARD_COMPOSE_H */
