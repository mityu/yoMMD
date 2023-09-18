# Configuration guide

You can write configuration in a TOML file.
About the TOML syntax, please refer to the official spec: https://toml.io/en/v1.0.0


## Configuration search path

yoMMD searchs configuration file from these places and take the first found one as configuration.

- `./config.toml` where `.` is the parent directory of the yoMMD executable
- `$XDG_CONFIG_HOME/yoMMD/config.toml`  (or `~/.config/yoMMD/config.toml` when `$XDG_CONFIG_HOME` isn't defined)
- `~/yoMMD/config.toml`


## Configuration keys

This is the list of available configuration items.

- `model`: string (required)

    Path to MMD model file (`.pmd` or `.pmx` file).
    You can write file path in the form of relative path or absolute path.
    If you write file path in relative path, `.` (current directory) will be the parent directory of the configuration file.

- `motion`: array of tables (optional, default: empty)

    Configuration about MMD motions.  Available keys in `motion` tables are here.

    - `path`: list of strings (required)

        List of paths to MMD motion files (`.vmd` files).
        You can write file path in the form of relative path or absolute path.
        If you write file path in relative path, `.` (current directory) will be the parent directory of the configuration file.

    - `weight`: integer (optional, default: 1)

        Specifies weight for this motion that used in weighted random motion selection.
        This value must be bigger than or equals to 1.

    - `disabled`: boolean (optional, default: false)

        When this value is `true`, the motion of this table is disabled.

- `default-model-position`: array of floats with 2 elements (optional, default: \[0, 0\])

    The default MMD model position on the main window.  Values should be specified in the order of \[x, y\], and the coordinate system is like this:

```
                  Y          +---------- yoMMD window (nearly equals to the screen)
                  ^          |
                  |1.0       v
     +------------|------------+
     |            |            |
     |            |            |
-1.0 |            |(0, 0)      | 1.0
  ----------------+---------------> X
     |            |            |
     |            |            |
     |            |            |
     +------------|------------+
                  |-1.0
                  |
```

- `default-camera-position`: array of floats with 3 elements (optional, default: \[0, 10, 50\])

    The default camera position on model world.  The value should be specified in the order of \[x, y, z\].  The coordinate system is right-handed coordinate system; right side of screen is where x > 0, upper side of screen is where y > 0, and front of screen is where z > 0.

```
                    Y

                    ^ positive
                    |
                    |
                    |
                    |   /  negative
                    |  /
                    | /
           (0, 0, 0)|/           positive
   -----------------+----------------> X
negative           /|
                  / |
                 /  |
                /   |
     positive  v    |
                    |
             Z      | negative
```

    Note: This option is to change camera position.  To change gaze position, see `default-gaze-position` option below.

    Note: Basically, the center of the MMD model position matches to the origin in the model world coordinate system.  (The exception is when MMD motion changed MMD model position.  In this case, the center of the MMD model position may differ from the origin of the model world.)

- `default-gaze-position`: array of floats with 3 elements (optional, default: \[0, 10, 0\])

    The default gaze position on model world.  The value should be specified in the order of \[x, y, z\].  The coordinate system is right-handed coordinate system, as described in `default-camera-potision` option.

- `default-scale`: float (optional, default: 1.0)

    The default scale factor of MMD model.

- `gravity`: float (optional, default: 9.8)

    The gravity value used in physics simulation.

- `simulation-fps`: float (optional, default: 60.0)

    This is a parameter of physics simulation.
    This value will passed to the third argument of `btDynamicsWorld::stepSimulation` function in the form of `1.0 / simulation-fps`.
    For the defails about `btDynamicsWorld::stepSimulation` function, please see:
    https://pybullet.org/Bullet/BulletFull/classbtDynamicsWorld.html#a5ab26a0d6e8b2b21fbde2ed8f8dd6294

- `default-screen-number`: integer (optional, default: the main screen's number)

    The default monitor number to show MMD model.  You can check the monitor number in "Select Screen" menu.  For example, if you specify `2` for this option, it's equals to apply "Select Screen" > "Screen2" menu item.
