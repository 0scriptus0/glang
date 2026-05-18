# G — G Language Interpreted

**File extension:** `.g`  
**Run:** `python3 g.py <file.gi>` or `python3 g.py` (REPL)

---

## Quick Tour

```java
// Variables
let name = "G"
let version = 0.1
let active = true

// Output / Input
printl("Hello, " + name)
let input = readl("> ")

// If / elif / else
if (score >= 90) {
    printl("A")
} elif (score >= 80) {
    printl("B")
} else {
    printl("C")
}

// While
while (running) {
    let cmd = readl("> ")
    if (cmd == "quit") { running = false; continue }
    printl("echo: " + cmd)
}

// For / in
for (i in range(1, 6)) { printl(i) }
for (item in myList) { printl(item) }

// Functions
func add(a, b) {
    return a + b
}

// Lambda (arrow syntax)
let double = func(x) -> x * 2

// Classes with inheritance
class Animal {
    func init(self, name) { self.name = name }
    func speak(self) { return self.name + " speaks" }
}

class Dog (Animal) {
    func speak(self) { return self.name + " says woof!" }
}

let d = new Dog("Rex")
printl(d.speak())

// Lists
let nums = [1, 2, 3]
push(nums, 4)
let doubled = map(func(x) -> x * 2, nums)

// Dicts
let person = {"name": "Alice", "age": 30}
printl(person["name"])

// With (context manager)
with (file("data.txt", "w")) as f {
    f.write("hello\n")
}

// loadlib — ctypes bridge
let user32 = loadlib("user32.dll")    // Windows
let ret = user32.MessageBoxA(0, "Hi", "G", 0)

// pass / continue / break
for (i in range(10)) {
    if (i == 5) { break }
    if (i % 2 == 0) { continue }
    printl(i)
}
```

## Built-in Functions

| Function | Description |
|---|---|
| `printl(...)` | Print with newline |
| `print(...)` | Print without newline |
| `readl(prompt)` | Read line from stdin |
| `len(x)` | Length of string/list/dict |
| `range(n)` / `range(a,b)` | Integer range list |
| `map(fn, list)` | Apply fn to each element |
| `filter(fn, list)` | Filter list by predicate |
| `reduce(fn, list, init?)` | Fold list |
| `push/pop/insert/remove` | List mutation |
| `split/join/trim/upper/lower` | String utilities |
| `int/float/str/bool` | Type coercion |
| `typeof(x)` | Type name string |
| `file(path, mode)` | Open file (use with `with`) |
| `loadlib(name)` | Load native library (ctypes) |
| `exit(code)` | Exit program |
| `sleep(s)` | Sleep N seconds |
| `json.parse(s)` | Parse JSON string |
| `json.stringify(v)` | Encode to JSON |
| `math.sqrt/sin/cos/pi/...` | Math functions |

## Keywords

`let` `if` `elif` `else` `while` `for` `in` `func` `return`  
`pass` `continue` `break` `with` `as` `class` `new` `import` `null` `true` `false`

## Operators

`+` `-` `*` `/` `%` `**` `==` `!=` `<` `>` `<=` `>=`  
`&&` `||` `!` `and` `or` `not`  
`=` `+=` `-=` `*=` `/=`

## Comments

```
// single line
/* multi
   line */
```
