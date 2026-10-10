PANE_MIN_WIDTH = 24


PANE_MIN_HEIGHT = 4


class Pane:
    __slots__ = ("conv", "scroll", "draft", "cursor", "hist", "hist_items", "stash")

    def __init__(self, conversation):
        self.conv = conversation
        self.scroll = 0
        self.draft = ""
        self.cursor = 0
        self.hist = None
        self.hist_items = None
        self.stash = ("", 0)


class Layout:
    def __init__(self):
        self.columns = []
        self.weights = []
        self.col = 0
        self.row = 0

    def panes(self):
        return [p for column in self.columns for p in column]

    def focused(self):
        if not self.columns:
            return None
        return self.columns[self.col][self.row]

    def find(self, conversation):
        for c, column in enumerate(self.columns):
            for r, pane in enumerate(column):
                if pane.conv == conversation:
                    return c, r
        return None

    def add(self, pane, vertical=True):
        if not self.columns:
            self.columns, self.weights, self.col, self.row = [[pane]], [1.0], 0, 0
        elif vertical:
            self.columns.insert(self.col + 1, [pane])
            self.weights.insert(self.col + 1, 1.0)
            self.col, self.row = self.col + 1, 0
        else:
            self.columns[self.col].insert(self.row + 1, pane)
            self.row += 1

    def close(self):
        if len(self.panes()) <= 1:
            return None
        pane = self.columns[self.col].pop(self.row)
        if not self.columns[self.col]:
            self.columns.pop(self.col)
            self.weights.pop(self.col)
            self.col = min(self.col, len(self.columns) - 1)
        self.row = min(self.row, len(self.columns[self.col]) - 1)
        return pane

    def focus(self, direction):
        if not self.columns:
            return
        if direction in ("left", "right"):
            self.col = max(0, min(len(self.columns) - 1, self.col + (1 if direction == "right" else -1)))
            self.row = min(self.row, len(self.columns[self.col]) - 1)
        else:
            self.row = max(0, min(len(self.columns[self.col]) - 1, self.row + (1 if direction == "down" else -1)))

    def move(self, direction):
        if not self.columns:
            return
        column = self.columns[self.col]
        if direction in ("up", "down"):
            other = self.row + (1 if direction == "down" else -1)
            if 0 <= other < len(column):
                column[self.row], column[other] = column[other], column[self.row]
                self.row = other
            return
        step = 1 if direction == "right" else -1
        target = self.col + step
        if not 0 <= target < len(self.columns):
            if len(column) == 1:
                return
            pane = column.pop(self.row)
            at = len(self.columns) if step > 0 else 0
            self.columns.insert(at, [pane])
            self.weights.insert(at, 1.0)
            self.col, self.row = at, 0
            return
        pane = column.pop(self.row)
        row = min(self.row, len(self.columns[target]))
        self.columns[target].insert(row, pane)
        if not column:
            self.columns.pop(self.col)
            self.weights.pop(self.col)
            if target > self.col:
                target -= 1
        self.col, self.row = target, row

    def resize(self, delta):
        if self.weights:
            self.weights[self.col] = max(0.3, min(3.0, self.weights[self.col] + delta))

    def geometry(self, width, height):
        n = len(self.columns)
        if not n:
            return []
        room = width - (n - 1)
        total = sum(self.weights)
        widths = [max(1, int(room * w / total)) for w in self.weights]
        widths[-1] = max(1, room - sum(widths[:-1]))
        if min(widths) < PANE_MIN_WIDTH:
            return None
        boxes, x = [], 0
        for c, column in enumerate(self.columns):
            k = len(column)
            usable = height - (k - 1)
            heights = [usable // k] * k
            heights[-1] += usable - sum(heights)
            if min(heights) < PANE_MIN_HEIGHT:
                return None
            y = 0
            for r, pane in enumerate(column):
                boxes.append((c, r, pane, x, y, widths[c], heights[r]))
                y += heights[r] + 1
            x += widths[c] + 1
        return boxes
