mruby-debugger
====

One of the debugger implementation for __mruby__.

# How to build
----

Edit your 'build_config.rb'

## mrbgem entry

    conf.gem "/path/to/your/mruby-debugger"

or

    conf.gem :github => 'crimsonwoods/mruby-debugger', :branch => 'master'

## compile flags

    config.cc do |cc|
      cc.defines << 'ENABLE_DEBUG'
    end

## run make

    $ make


# How to use
----

## class Debugger

    # activate debugger.
    Debugger.start

    # launch debugger interface.
    Debugger.debugger
