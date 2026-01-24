dot counter:
    state:
        count: 0
        max: 100

    when count < max:
        do:
            add count 1
