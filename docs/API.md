# plugin.box2dv3 — API reference

Plugin version **0.7.0**, Box2D **3.2.0**.

```lua
local physics = require( "plugin.box2dv3" )
```

The API keeps the syntax of Solar2D's built-in `physics` library (Box2D 2.x) and adds Box2D v3 features.
Anything marked **v3** or **new** does not exist in the built-in library; anything marked **changed** behaves differently from the built-in library
(usually a v2 bug or inconsistency that was fixed).

## Contents

- [Units](#units)
- [World](#world)
- [Bodies — `physics.addBody`](#bodies--physicsaddbody)
- [Body properties](#body-properties)
- [Body methods](#body-methods)
- [Collision events](#collision-events)
- [Joints — `physics.newJoint`](#joints--physicsnewjoint)
- [Queries](#queries)
- [Explosions](#explosions)
- [Character mover](#character-mover)
- [Save / restore](#save--restore)
- [Debug draw](#debug-draw)
- [Multithreading](#multithreading)
- [Differences from the built-in physics library](#differences-from-the-built-in-physics-library)

---

## Units

Every value the plugin takes or returns uses the same units:

| Quantity | Unit |
|---|---|
| position, length, radius | pixels (content units) |
| angle, rotation | degrees |
| linear velocity | pixels / second |
| angular velocity | degrees / second |
| force | Newtons |
| torque | N·m |
| linear impulse | N·s (kg·m/s) |
| angular impulse | kg·m²/s |
| mass | kg |
| density | kg / m² |

The pixel ↔ meter ratio is set with `physics.setScale()` (default 30 pixels per meter). Forces and impulses
are in SI units, as in the built-in library.

---

## World

### physics.start( [noSleep] )

Creates the world (if needed) and starts the simulation. With `noSleep = true` bodies never fall asleep.
Calling it again after `physics.pause()` resumes the simulation.

### physics.pause()

Stops stepping. Debug draw is still rendered.

### physics.stop() → boolean

Destroys the world: every body, joint and mover is removed, and states saved with `physics.saveState()` become
invalid. Cannot be called from inside `preCollision`.

### physics.setGravity( gx, gy ) / physics.getGravity() → gx, gy

Gravity in m/s² (default `0, 9.8`). Setting it wakes every dynamic body.

### physics.setScale( pixelsPerMeter ) / physics.getScale()

Default `30`. Set it before adding bodies.

### physics.setTimeStep( dt )

| `dt` | Behaviour |
|---|---|
| `< 0` (default) | one step per frame, of length `1 / display.fps` |
| `0` | time based: fixed steps of `1 / display.fps` that follow real time (at most 8 steps per frame) |
| `> 0` | one step of `dt` seconds per frame |

### physics.setTimeScale( scale ) / physics.getTimeScale()

Slow motion (`< 1`) or fast forward (`> 1`). Default `1`.

### physics.setSubSteps( n ) / physics.getSubSteps() — v3

Number of solver sub-steps per step (default `4`). More sub-steps give stiffer stacks and joints.
Replaces `setVelocityIterations` / `setPositionIterations`, which are accepted but have no effect.

### physics.setContactTuning( hertz, [dampingRatio], [pushSpeed] ) — v3

Contact stiffness (Box2D defaults: `30`, `10`, `3` m/s). Box2D caps the stiffness at 1/8 of the sub-step
rate, so raise `setSubSteps()` together with `hertz`. Stiffer contacts sink less in tall stacks or with large
mass ratios.

### physics.getContactTuning() → hertz, dampingRatio, pushSpeed, effectiveHertz — v3

`effectiveHertz` is the stiffness actually used after Box2D's cap.

### physics.setContinuous( enabled )

Continuous collision for fast dynamic bodies against static ones (default on). `obj.isBullet` enables it
against dynamic bodies too.

### physics.setHitEventThreshold( speed ) / physics.getHitEventThreshold() — v3

Minimum approach speed (px/s) for [`"hit"` events](#hit-event--v3). Default 30 px/s (1 m/s).

### physics.setThreadCount( n ) / physics.getThreadCount() → configured, used — v3

See [Multithreading](#multithreading).

### physics.toMKS( unit, value ) / physics.fromMKS( unit, value )

Converts between pixel and MKS units. `unit`: `"length"`, `"velocity"` (pixels ↔ meters) or
`"angularVelocity"` (degrees ↔ radians).

### physics.setMKS( key, value ) → boolean / physics.getMKS( key )

Values in MKS units.

| Key | Meaning |
|---|---|
| `velocityThreshold` | restitution threshold, m/s |
| `linearSleepTolerance` | default sleep threshold of new bodies, m/s |
| `contactHertz`, `contactDampingRatio`, `contactPushSpeed` | v3, same as `setContactTuning` |
| `maxLinearSpeed` | v3, speed limit, m/s |
| `timeToSleep`, `maxSubSteps`, `angularSleepTolerance` | not available in Box2D v3: warning, returns `false` |

### physics.setReportCollisionsInContentCoordinates( enabled ) / get…

When `true` (**changed:** the default), `event.x` / `event.y` of collision events are content coordinates.
When `false`, they are local to `object1`, as in the built-in library.

### physics.setAverageCollisionPositions( enabled ) / get…

When a contact has two points, report their average instead of the first point.

### physics.setDebugErrorsEnabled( enabled ) / get…

Accepted for compatibility (no effect).

### physics.getStats() → table — new

| Field | Meaning |
|---|---|
| `bodyCount`, `shapeCount` | bodies / shapes created by the plugin |
| `worldBodyCount`, `awakeBodyCount`, `contactCount`, `jointCount`, `islandCount` | Box2D counters |
| `stepTime`, `syncTime`, `eventTime`, `frameTime` | milliseconds spent in the last frame (Box2D step, display sync, event dispatch, total) |
| `stepCount` | steps taken in the last frame |
| `threadCount` | threads used by the last step |
| `running` | `true` while the world is running |

### physics.version, physics.engineVersion

Plugin version (`"0.7.0"`) and Box2D version (`"3.2.0"`).

---

## Bodies — `physics.addBody`

```lua
physics.addBody( object, [bodyType], [element1, element2, ...] ) → boolean
```

`bodyType`: `"dynamic"` (default), `"static"` or `"kinematic"`. Each element is a table describing one shape of
the body. With no element, the body is a rectangle the size of the object.

### Shape of an element

Fields are tried in this order; the first one present defines the element.

| Field | Shape |
|---|---|
| `circle = { radius, x, y }` | circle at an offset from the object centre — new |
| `capsule = { x1, y1, x2, y2, radius }` | capsule; `capsule = true` (or missing fields) fits the object along its longer side — v3 |
| `shape = { x1, y1, x2, y2, ... }` | convex polygon, at most 8 vertices, relative to the object centre. A concave polygon is replaced by its convex hull (with a warning). |
| `box = { halfWidth, halfHeight, x, y, angle }` | rectangle with an offset and angle |
| `chain = { x1, y1, ... }` | chain of segments, collides on both sides; `connectFirstAndLastChainVertex = true` closes it |
| `radius = r` | circle centred on the object |
| `outline = graphics.newOutline( ... )` | any outline (concave allowed): split into convex polygons, one element each |
| *(none)* on a `display.newPolygon` | **static:** the exact outline (chain); **dynamic:** the bounding rectangle |
| *(none)* on a `display.newLine` | open chain along the line (always static) |
| *(none)* | rectangle the size of the object |

`display.newPolygon`, `display.newLine` (including `line:append()`) and `graphics.newOutline` are wrapped by
the plugin so it can read their vertices: `require( "plugin.box2dv3" )` before creating those objects.

### Material and options of an element

| Field | Default | Meaning |
|---|---|---|
| `density` | `1` | kg/m² |
| `friction` | `0.3` | |
| `bounce` | `0.2` | restitution |
| `isSensor` | `false` | detects overlaps without colliding |
| `filter` | collide with everything | `{ categoryBits, maskBits, groupIndex }` (64-bit masks) |
| `cornerRadius` | `0` | **v3** rounded corners for `box`, `shape` and the default rectangle, in pixels; the outer size is kept |
| `rollingResistance` | `0` | **v3** 0..1, slows rolling circles / capsules |
| `tangentSpeed` | `0` | **v3** surface speed in px/s (conveyor belts) |

```lua
physics.addBody( crate, { density = 2, friction = 0.6, cornerRadius = 3 } )
physics.addBody( car, "dynamic",
    { shape = { -40,-10, 40,-10, 40,10, -40,10 }, density = 3 },
    { radius = 8, isSensor = true } )
physics.addBody( belt, "static", { friction = 1, tangentSpeed = 120 } )
```

### physics.removeBody( object ) → boolean

Removes the body and its joints; the display object stays. Removing the display object
(`display.remove( object )`, `object:removeSelf()`) removes its body immediately.

---

## Body properties

All are read / write unless noted.

| Property | Meaning |
|---|---|
| `x`, `y`, `rotation` | assigning teleports the body; `rotation` accumulates past 360 |
| `bodyType` | `"static"`, `"dynamic"`, `"kinematic"` |
| `isAwake` | |
| `isBodyActive` | inactive bodies are removed from the simulation |
| `isBullet` | continuous collision against dynamic bodies |
| `isSleepingAllowed` | |
| `isFixedRotation` | |
| `isFixedX`, `isFixedY` | **v3** lock the motion along one axis |
| `isSensor` | **changed:** readable. Setting it rebuilds the shapes of the body |
| `angularVelocity` | deg/s |
| `linearDamping`, `angularDamping` | |
| `gravityScale` | |
| `sleepThreshold` | **v3** px/s under which the body may sleep |
| `tangentSpeed` | **v3** px/s, set on every element; reads element 1 |
| `rollingResistance` | **v3** set on every element; reads element 1 |
| `mass` | read-only, kg |

---

## Body methods

| Method | Meaning |
|---|---|
| `obj:setLinearVelocity( vx, vy )` / `obj:getLinearVelocity()` | px/s |
| `obj:applyForce( fx, fy, x, y )` | N at a world point (px) |
| `obj:applyForceToCenter( fx, fy )` | **v3** N at the centre of mass |
| `obj:applyTorque( t )` | N·m |
| `obj:applyLinearImpulse( ix, iy, x, y )` | N·s at a world point |
| `obj:applyLinearImpulseToCenter( ix, iy )` | **v3** N·s at the centre of mass |
| `obj:applyAngularImpulse( i )` | **changed:** a real angular impulse |
| `obj:setTargetTransform( x, y, [rotation], [time] )` | **v3** kinematic bodies: sets the velocity that reaches the target in `time` seconds (default one frame), so the body pushes others instead of teleporting through them. Box2D limits rotation to 45° per step. |
| `obj:resetMassData()` | recompute mass from the shapes |
| `obj:getMassWorldCenter()` / `obj:getMassLocalCenter()` | px |
| `obj:getInertia()` | **changed:** kg·m² about the centre of mass |
| `obj:getWorldVector( lx, ly )` | **changed:** rotates a vector, same units in and out |
| `obj:getLinearVelocityFromWorldPoint( x, y )` / `…FromLocalPoint( x, y )` | **changed:** px/s |
| `obj:translate( dx, dy )`, `obj:rotate( deg )` | move the body with the object |

---

## Collision events

Listeners are added as in the built-in library, on the object (`obj:addEventListener( "collision", f )`,
`obj.collision = f`) or on `Runtime`. An event goes to `object1`, then `object2`, then `Runtime`, and stops at the
first listener that returns `true`.

| Field | Local listener | Runtime listener |
|---|---|---|
| objects | `target` (the object), `other` | `object1`, `object2` |
| elements | `selfElement`, `otherElement` (index in `addBody`) | `element1`, `element2` |
| position | `x`, `y` | `x`, `y` |

### "collision"

`event.phase` is `"began"` or `"ended"`. Also `event.contact`. **Changed:** dispatched right after the step,
so bodies can be added, removed or changed inside the listener without `timer.performWithDelay`.
Sensors report `"began"` / `"ended"` too (v3 sensors do not detect other sensors).

### "preCollision"

Dispatched during the step, before the contact is solved. Set `event.contact.isEnabled = false` to ignore the
contact (one-way platforms). Objects removed here are destroyed at the end of the step. While any
`preCollision` listener exists the simulation runs on one thread.

`event.contact`: `isEnabled` (writable in `preCollision`), `isTouching`, `friction`, `bounce`, `tangentSpeed`,
`normalX`, `normalY` (read-only — set materials on the element instead).

### "postCollision"

After the step, for contacts with a listener. `event.force` = largest normal impulse, `event.friction` =
largest tangent impulse.

### "hit" — v3

A contact began with an approach speed above `physics.setHitEventThreshold()`. Fields: `approachSpeed`
(px/s), `normalX`, `normalY` (from object1 to object2), `x`, `y`, plus the object / element fields above.
Use it for impact sounds and damage.

### "jointBreak" — v3

See [breakable joints](#breakable-joints--v3).

Errors in listeners are raised as normal Lua errors after the step.

---

## Joints — `physics.newJoint`

```lua
local joint = physics.newJoint( type, objectA, objectB, ... )
```

| Type | Arguments after the two objects | Box2D v3 joint |
|---|---|---|
| `"pivot"` | `anchorX, anchorY` | revolute |
| `"distance"` | `anchorAx, anchorAy, anchorBx, anchorBy` | distance (rigid; spring when `frequency > 0`) |
| `"piston"` | `anchorX, anchorY, axisX, axisY` | prismatic |
| `"wheel"` | `anchorX, anchorY, axisX, axisY` | wheel (spring 2 Hz / 0.7) |
| `"weld"` | `anchorX, anchorY` | weld |
| `"friction"` | `anchorX, anchorY` | motor joint with zero target velocity |
| `"rope"` | `offsetAx, offsetAy, offsetBx, offsetBy, [maxLength]` | distance joint with a maximum length; offsets from each body's origin; default length = current distance between the anchors |
| `"touch"` | *(one object)* `physics.newJoint( "touch", obj, x, y )` | motor joint to a hidden kinematic body |
| `"motor"` | *(none)* | **v3** motor joint |
| `"pulley"`, `"gear"` | — | not in Box2D v3: error |

Anchors and axes are in content coordinates.

### Common properties and methods

| Member | Meaning |
|---|---|
| `jointType` | **v3** read-only type name |
| `isActive` | read-only |
| `isCollideConnected` | **changed:** writable |
| `reactionTorque` | read-only, N·m |
| `breakForce`, `breakTorque` | **v3** see below |
| `joint:getAnchorA()`, `joint:getAnchorB()` | world anchors, px |
| `joint:getLocalAnchorA()`, `joint:getLocalAnchorB()` | px |
| `joint:getReactionForce()` | N |
| `joint:removeSelf()` | |

A joint whose body was removed (or after `physics.stop()`) reads `nil` for every property, and its methods
raise an error.

### Per-type members

| Type | Members |
|---|---|
| `pivot` | `isMotorEnabled`, `motorSpeed` (deg/s), `maxMotorTorque`, `motorTorque` (read-only), `referenceAngle`, `jointAngle`, `jointSpeed`, `isLimitEnabled`, `setRotationLimits( lo, hi )`, `getRotationLimits()`, `springFrequency`, `springDampingRatio` (**v3**) |
| `distance` | `length`, `frequency`, `dampingRatio` |
| `piston` | `isMotorEnabled`, `motorSpeed` (px/s), `maxMotorForce`, `motorForce`, `getLocalAxisA()`, `referenceAngle`, `jointTranslation`, `jointSpeed`, `isLimitEnabled`, `setLimits( lo, hi )`, `getLimits()` |
| `wheel` | `isMotorEnabled`, `motorSpeed` (**changed:** deg/s), `maxMotorTorque`, `motorTorque` (**changed:** current torque), `getLocalAxisA()` (**changed:** the real axis), `jointTranslation`, `jointSpeed`, `springFrequency`, `springDampingRatio`, `isLimitEnabled`, `setLimits()`, `getLimits()` (**v3**) |
| `weld` | `referenceAngle`, `frequency`, `dampingRatio` (angular softness) |
| `friction` | `maxForce`, `maxTorque` |
| `rope` | `maxLength` (**changed:** px both ways), `limitState` (`"upper"` / `"inactive"`) |
| `touch` | `maxForce`, `frequency`, `dampingRatio`, `setTarget( x, y )`, `getTarget()` |
| `motor` | see below |

### "motor" joint — v3

Holds `objectB` at its current offset and angle relative to `objectA` with a spring (4 Hz, 0.7,
`maxSpringForce` = 1000 × mass).

| Member | Meaning |
|---|---|
| `joint:setLinearOffset( x, y )` / `getLinearOffset()` | target position of B in A's frame, px |
| `angularOffset` | target angle, deg |
| `joint:setLinearVelocity( vx, vy )` / `getLinearVelocity()` | velocity drive, px/s (needs `maxForce > 0`) |
| `angularVelocity` | deg/s (needs `maxTorque > 0`) |
| `maxForce`, `maxTorque` | velocity drive limits (default 0 = off) |
| `linearFrequency`, `linearDampingRatio`, `angularFrequency`, `angularDampingRatio` | spring |
| `maxSpringForce`, `maxSpringTorque` | spring limits |

```lua
local anchor = display.newRect( 240, 100, 4, 4 ); physics.addBody( anchor, "static", { isSensor = true } )
local platform = display.newRect( 240, 200, 80, 10 ); physics.addBody( platform )
local m = physics.newJoint( "motor", anchor, platform )
m:setLinearOffset( 50, 100 )   -- glides to 50 px right of the anchor, 100 px below it
```

### Breakable joints — v3

```lua
joint.breakForce = 200    -- N    (default math.huge; nil = unbreakable)
joint.breakTorque = 50    -- N·m
```

When the constraint force exceeds a threshold, a `"jointBreak"` event is dispatched after the step
(`object1` → `object2` → `Runtime`), then the joint is removed. Fields: `joint`, `target` / `other` (local) or
`object1` / `object2` (Runtime), `force`, `torque`. `force` / `torque` are the values at the end of the step
and can be below the threshold, because Box2D checks the peak inside each sub-step.

```lua
Runtime:addEventListener( "jointBreak", function( e ) audio.play( snapSound ) end )
```

---

## Queries

`options` (optional, last argument): `{ categoryBits = n, maskBits = n, ignoreSensors = boolean }`.

### physics.rayCast( x1, y1, x2, y2, [behavior], [options] ) → hits | nil

`behavior`: `"closest"` (default), `"any"`, `"unsorted"`, `"sorted"`. Each hit:
`object`, `element`, `position = { x, y }`, `normal = { x, y }`, `fraction`.

### physics.reflectRay( x1, y1, hit ) → dx, dy

Normalised reflected direction of a ray that started at `(x1, y1)`.

### physics.circleCast( x1, y1, x2, y2, radius, [behavior], [options] ) → hits | nil — v3

Sweeps a circle; hits as `rayCast` plus `center = { x, y }`, the circle's centre when it touches.

### physics.queryRegion( x1, y1, x2, y2, [options] ) → objects | nil

**Changed:** objects whose shapes overlap the rectangle (exact test), each listed once.

### physics.queryPoint( x, y, [options] ) → objects | nil — new

### physics.queryCircle( x, y, radius, [options] ) → objects | nil — v3

---

## Explosions

### physics.explode( x, y, radius, impulsePerLength, [falloff], [maskBits] ) — v3

Pushes bodies away from `(x, y)`. `radius` and `falloff` in px (the impulse fades to zero over `falloff`
beyond `radius`). `impulsePerLength` is in N·s per meter of shape outline facing the explosion; negative
values pull bodies in. Affects circles, capsules and polygons.

```lua
physics.explode( event.x, event.y, 80, 2, 40 )
```

---

## Character mover

### physics.newMover( object, [options] ) → mover — v3

A game-style character controller (Box2D's "geometric mover"): a capsule is swept through the world and pushed
out of shapes; a spring "leg" keeps it above the ground so it walks up steps and slopes. It is not a body:
other bodies do not collide with it, but it pushes dynamic bodies and presses on what it stands on. The object
must not have a body. The object's box is the capsule on top and the legs (`stepHeight`) below; at rest the
bottom of the object touches the ground.

| Option / property | Default | Meaning |
|---|---|---|
| `radius` | half the object width | px |
| `height` | object height | px, including the legs |
| `stepHeight` | `height / 4` | px, highest step it walks up |
| `maxSpeed` | 6 m/s (180 px/s) | px/s |
| `jumpHeight` | `1.5 × height` | px (`jumpSpeed` is derived) |
| `jumpSpeed` | — | px/s, alternative to `jumpHeight` |
| `gravity` | world gravity | px/s² |
| `acceleration` | `20` | ground acceleration factor (1/s) |
| `airControl` | `0.2` | fraction of acceleration in the air |
| `friction` | `8` | ground friction (1/s) |
| `stopSpeed` | 3 m/s | px/s |
| `pogoFrequency`, `pogoDampingRatio` | `5`, `0.8` | leg spring |
| `pushBodies` | `true` | push dynamic bodies |
| `groundForce` | `50` | N pressed on a dynamic body underneath |
| `filter` | everything | `{ categoryBits, maskBits }` (option only) |

| Member | Meaning |
|---|---|
| `mover.throttle` | horizontal input −1..1, kept until changed |
| `mover:jump()` → boolean | `false` when not on the ground |
| `mover.isOnGround` | read-only |
| `mover.groundObject` | read-only, the object it stands on |
| `mover:getVelocity()` / `mover:setVelocity( vx, vy )` | px/s |
| `mover.x`, `mover.y` | assigning teleports (assigning `object.x / y` does too) |
| `mover:removeSelf()` | removing the object also removes the mover |

```lua
local hero = display.newRect( 60, 200, 16, 32 )
local mover = physics.newMover( hero, { maxSpeed = 140, jumpHeight = 70 } )
Runtime:addEventListener( "key", function( e )
    if e.keyName == "right" then mover.throttle = e.phase == "down" and 1 or 0 end
    if e.keyName == "space" and e.phase == "down" then mover:jump() end
end )
```

The mover updates before every step. It does not ride moving platforms.

---

## Save / restore

### physics.saveState() → state — v3

### physics.restoreState( state ) → removedObjects — v3

Puts every body (position, rotation, velocity, sleep state, body type…), contact, joint and mover back to the
saved instant; replaying from the same state gives the same result. For undo, rollback, checkpoints.

- Bodies added after the save are removed (like `physics.removeBody`); their objects are returned so the app
  can delete them.
- Objects removed since the save stay removed; joints created after the save are removed; joints removed since
  the save do not come back.
- Plugin settings (gravity, contact tuning, …) are kept as they are now.
- A state only works with the world that saved it. `state.size` (bytes), `state.bodyCount`.

```lua
local checkpoint = physics.saveState()
-- later
for _, o in ipairs( physics.restoreState( checkpoint ) ) do display.remove( o ) end
```

Neither function can be called inside `preCollision`.

---

## Debug draw

### physics.setDrawMode( mode, [drawContacts] ) / physics.getDrawMode()

`mode`: `"normal"`, `"hybrid"` (shapes drawn over the scene) or `"debug"` (shapes only, on an opaque
background). `drawContacts = true` also draws contact points. The overlay is made of display objects rebuilt
every frame — for debugging only.

The overlay is drawn **inside the group that holds the bodies** (the parent of their display objects), on top
of it, so it follows that group when it is moved, scaled or rotated (a camera / zoom group). UI outside that
group stays above the overlay.

### physics.setDrawGroup( [group] ) — new

Draws the overlay inside `group` instead. Use it when the bodies are spread over several child groups that
share the same coordinates. `nil` goes back to automatic.

---

## Multithreading

### physics.setThreadCount( n ) — v3

Number of threads Box2D uses, including the main thread (default 1). Helps large scenes (hundreds of
awake bodies). Steps that have a `preCollision` listener run on one thread automatically, because Lua runs
inside the step; removing the listener restores the thread count. `physics.getThreadCount()` returns the
configured count and the count used by the last step.

---

## Differences from the built-in physics library

The plugin keeps the syntax but does not copy v2 bugs. Main differences:

- Default material: density 1, friction 0.3, bounce 0.2, for every element (as documented by Solar2D).
- Default `filter` collides with everything (64-bit masks).
- `setVelocityIterations` / `setPositionIterations` have no effect; use `setSubSteps`.
- `collision` events are dispatched after the step; listeners may change the world directly.
- Collision positions are content coordinates by default.
- Fixed v2 bugs: `applyAngularImpulse`, `getWorldVector`, `getLinearVelocityFrom*Point`, `getInertia`,
  `wheel.motorSpeed` / `motorTorque`, `rope.maxLength`, `getLocalAxisA`; `obj.isSensor` is readable;
  `queryRegion` tests shapes exactly.
- `pulley` and `gear` joints do not exist in Box2D v3.
- v3 sensors do not detect other sensors.
- `rotation` uses exact trigonometry, so `obj.rotation = 30` reads back `30`.
