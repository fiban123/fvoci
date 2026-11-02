"""Small VT screen reader for assertions about ncurses output (stdlib only)."""
import codecs
import re
import unicodedata


class Screen:
    def __init__(self, rows=30, columns=110):
        self.rows, self.columns = rows, columns
        self.row = self.column = 0
        self.fg = self.bg = None
        self.bold = self.dim = self.reverse = False
        self.graphics = False
        self.cursor_visible = True
        self.cursor_shape = 0
        self.saved = (0, 0)
        self.scroll_top, self.scroll_bottom = 0, rows - 1
        self.pending = ''
        self.decoder = codecs.getincrementaldecoder('utf-8')('replace')
        self.cells = [[self.blank() for _ in range(columns)] for _ in range(rows)]

    def blank(self):
        return {'text': ' ', 'fg': self.fg, 'bg': self.bg,
                'bold': self.bold, 'dim': self.dim, 'reverse': self.reverse}

    def resize(self, rows, columns):
        old = self.cells
        self.rows, self.columns = rows, columns
        self.cells = [[old[y][x] if y < len(old) and x < len(old[y]) else self.blank()
                       for x in range(columns)] for y in range(rows)]
        self.row = min(self.row, rows - 1)
        self.column = min(self.column, columns - 1)
        self.scroll_top, self.scroll_bottom = 0, rows - 1

    @property
    def lines(self):
        return [''.join(cell['text'] for cell in row) for row in self.cells]

    @property
    def text(self):
        return '\n'.join(self.lines)

    def colors_of(self, text):
        for y, line in enumerate(self.lines):
            x = line.find(text)
            if x >= 0:
                return [self.cells[y][i]['fg'] for i in range(x, x + len(text))]
        raise AssertionError(f'{text!r} not visible:\n{self.text}')

    def clear_line(self, start, end):
        for x in range(max(0, start), min(self.columns, end)):
            self.cells[self.row][x] = self.blank()

    def csi(self, parameters, command):
        private = parameters.startswith('?')
        parameters = parameters.lstrip('?=>')
        # Direct-color terminfo uses colon-separated RGB with an empty colorspace.
        parameters = parameters.replace(':2::', ';2;').replace(':', ';')
        values = [int(x or 0) for x in parameters.split(';')] if parameters else [0]
        n = values[0] or 1
        if command in ('h', 'l') and private and 25 in values:
            self.cursor_visible = command == 'h'
        elif command == 'q': self.cursor_shape = values[0]
        elif command in 'Hf':
            self.row = min(self.rows - 1, max(0, n - 1))
            self.column = min(self.columns - 1, max(0, (values[1] or 1) - 1)) if len(values) > 1 else 0
        elif command == 'd': self.row = min(self.rows - 1, n - 1)
        elif command in 'G`': self.column = min(self.columns - 1, n - 1)
        elif command == 'A': self.row = max(0, self.row - n)
        elif command == 'B': self.row = min(self.rows - 1, self.row + n)
        elif command == 'C': self.column = min(self.columns - 1, self.column + n)
        elif command == 'D': self.column = max(0, self.column - n)
        elif command == 'K':
            mode = values[0]
            self.clear_line(0 if mode in (1, 2) else self.column,
                            self.columns if mode in (0, 2) else self.column + 1)
        elif command == 'J':
            mode = values[0]
            for y in range(self.rows):
                for x in range(self.columns):
                    if mode == 2 or (mode == 0 and (y, x) >= (self.row, self.column)) or (mode == 1 and (y, x) <= (self.row, self.column)):
                        self.cells[y][x] = self.blank()
        elif command == 'X': self.clear_line(self.column, self.column + n)
        elif command == 'P':
            row = self.cells[self.row]
            self.cells[self.row] = (row[:self.column] + row[self.column+n:] + [self.blank() for _ in range(n)])[:self.columns]
        elif command == '@':
            row = self.cells[self.row]
            self.cells[self.row] = (row[:self.column] + [self.blank() for _ in range(n)] + row[self.column:])[:self.columns]
        elif command in ('L', 'M', 'S', 'T'):
            start = self.row if command in ('L', 'M') else self.scroll_top
            end = self.scroll_bottom + 1
            n = min(n, end - start)
            blanks = [[self.blank() for _ in range(self.columns)] for _ in range(n)]
            if command in ('M', 'S'):
                self.cells[start:end] = self.cells[start+n:end] + blanks
            else:
                self.cells[start:end] = blanks + self.cells[start:end-n]
        elif command == 'r' and not private:
            self.scroll_top = max(0, n - 1)
            self.scroll_bottom = min(self.rows - 1, (values[1] or self.rows) - 1) if len(values) > 1 else self.rows - 1
            self.row = self.column = 0
        elif command == 'b':
            previous = self.cells[self.row][max(0, self.column - 1)]['text']
            for _ in range(n): self.put(previous)
        elif command == 'm':
            i = 0
            while i < len(values):
                value = values[i]
                i += 1
                if value in (38, 48) and i + 3 < len(values) and values[i] == 2:
                    rgb = tuple(values[i+1:i+4])
                    if value == 38: self.fg = rgb
                    else: self.bg = rgb
                    i += 4
                    continue
                if value == 0: self.fg = self.bg = None; self.bold = self.dim = self.reverse = False
                elif value == 1: self.bold = True
                elif value == 2: self.dim = True
                elif value == 7: self.reverse = True
                elif value == 22: self.bold = self.dim = False
                elif value == 27: self.reverse = False
                elif 30 <= value <= 37: self.fg = value - 30
                elif value == 39: self.fg = None
                elif 40 <= value <= 47: self.bg = value - 40
                elif value == 49: self.bg = None
        elif command == 'h' and private and 1049 in values:
            self.cells = [[self.blank() for _ in range(self.columns)] for _ in range(self.rows)]
            self.row = self.column = 0

    def put(self, char):
        if self.graphics:
            char = {'q': '─', 'x': '│', 'l': '┌', 'k': '┐', 'm': '└', 'j': '┘'}.get(char, char)
        if unicodedata.combining(char):
            self.cells[self.row][max(0, self.column-1)]['text'] += char
            return
        if self.column >= self.columns:
            self.column = 0
            self.row = min(self.rows-1, self.row+1)
        cell = self.blank()
        cell['text'] = char
        self.cells[self.row][self.column] = cell
        self.column += 2 if unicodedata.east_asian_width(char) in ('W', 'F') else 1

    def feed(self, data):
        self.pending += self.decoder.decode(data)
        while self.pending:
            char = self.pending[0]
            if char == '\x1b':
                if len(self.pending) < 2: return
                if self.pending[1] == '[':
                    match = re.match(r'\x1b\[([0-?]*)([ -/]*)([@-~])', self.pending)
                    if not match: return
                    self.csi(match[1], match[3])
                    self.pending = self.pending[match.end():]
                    continue
                if self.pending[1] in '()':
                    if len(self.pending) < 3: return
                    if self.pending[1] == '(': self.graphics = self.pending[2] == '0'
                    self.pending = self.pending[3:]
                    continue
                if self.pending[1] == '7': self.saved = (self.row, self.column)
                if self.pending[1] == '8': self.row, self.column = self.saved
                self.pending = self.pending[2:]
                continue
            self.pending = self.pending[1:]
            if char == '\r': self.column = 0
            elif char == '\n': self.row = min(self.rows-1, self.row+1)
            elif char == '\b': self.column = max(0, self.column-1)
            elif char == '\t': self.column = min(self.columns-1, (self.column//8+1)*8)
            elif ord(char) >= 32: self.put(char)
