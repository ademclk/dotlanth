import "stdlib"

dot greeter:
    state:
        name: "world"
        greeting: ""

    when greeting == "":
        do:
            emit "hello {name}"
