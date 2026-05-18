func print(value) {
    return STDOUT(value)
}

func printf(value) {
    return CNLSTDOUT(value)
}

func readl(prompt) {
    return STDIN(prompt)
}

func readl_buf() {
    return STDIN_BUFFER()
}

func read_file(path) {
    let f = file(path, "r")
    let data = f.read()
    f.close()
    return data
}

func write_file(path, content) {
    let f = file(path, "w")
    f.write(content)
    f.close()
    return true
}

func append_file(path, content) {
    let f = file(path, "a")
    f.write(content)
    f.close()
    return true
}

func exec(cmd) {
    return system(cmd)
}