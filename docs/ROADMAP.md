# plugin.box2dv3 — lộ trình

Mục tiêu: `local physics = require("plugin.box2dv3")` dùng được như `require("physics")` (Box2D 2.x
tích hợp sẵn trong Solar2D), chạy trên Box2D 3.2, và thêm các tính năng mới của v3.

Kiến trúc: toàn bộ bằng C/C++ (giống spinePlugin của depilz), không có Lua nhúng.

- `shared/include`, `shared/src`: Box2D 3.2.0 (không sửa).
- `shared/plugin`: phần binding Lua.
  - `b2plugin_main.cpp`: entry `luaopen_plugin_box2dv3`, vòng đời world, bước mô phỏng trong `enterFrame`.
  - `b2plugin_body.cpp`: `addBody`/`removeBody`, gắn body vào display object.
- Mỗi display object có body sẽ nhận metatable riêng (bản sao metatable của Solar2D). `__index`/`__newindex`
  xử lý trước các khóa vật lý (`obj.bodyType`, `obj.isAwake`, `obj:setLinearVelocity`…), còn lại chuyển
  về hàm gốc của Solar2D. Nhờ vậy cú pháp v2 vẫn giữ được: gán `obj.x` hay gọi `obj:translate()` sẽ dịch
  chuyển body như trước.
- Body tự bị hủy khi display object bị xóa. Solar2D chỉ "mồ côi" object khi `removeSelf()`/`display.remove()` và
  phát `finalize` muộn hơn (lúc dọn object cuối frame), nên plugin chặn `removeSelf` của object có body để huỷ body
  ngay lập tức. Khi xoá cả một group cha, body của các con bị huỷ lúc Solar2D phát `finalize` (cuối frame), giống v2.

## Các bước

| Bước | Nội dung | Trạng thái |
|---|---|---|
| 1 | Lõi: `start/pause/stop`, gravity, scale, timeStep/timeScale, `setSubSteps` (mới), `addBody` (mặc định hình chữ nhật, `radius`, `circle`, `box`, `shape`, nhiều element, `filter`, vật liệu), `removeBody`, đồng bộ vị trí/góc, teleport qua `obj.x/y/rotation`, `translate/rotate`, tự xóa body khi xóa object; thuộc tính `bodyType`, `isAwake`, `angularVelocity`, `isBodyActive`, `get/setLinearVelocity` | ✅ |
| 2 | Toàn bộ thuộc tính/phương thức của body theo v2 (`applyForce`, `applyLinearImpulse`, `applyTorque`, `applyAngularImpulse`, `isBullet`, `isFixedRotation`, `isSleepingAllowed`, `gravityScale`, damping, `mass`, `isSensor`, `getMass*Center`, `getInertia`, `getWorldVector`, `getLinearVelocityFrom*Point`, `resetMassData`), `chain` (+`connectFirstAndLastChainVertex`), `outline` (tách đa giác lõm), `display.newLine` tĩnh, `display.newPolygon` tĩnh, `toMKS/fromMKS`, `setMKS/getMKS` | ✅ |
| 3 | Va chạm: `collision` (began/ended, Runtime + local, sensor), `preCollision` (`event.contact.isEnabled`), `postCollision` (`force`, `friction`), lan truyền object1 → object2 → Runtime như v2, `selfElement/otherElement`, `setReportCollisionsInContentCoordinates`, `setAverageCollisionPositions` | ✅ |
| 4 | Joint: `pivot`, `distance`, `piston`, `wheel`, `weld`, `friction`, `rope`, `touch` cùng toàn bộ thuộc tính/phương thức joint của v2 (`getAnchorA/B`, `getLocalAnchorA/B`, `getReactionForce`, `reactionTorque`, motor, limit, spring, `setTarget`…), `joint:removeSelf()` | ✅ |
| 5 | Truy vấn: `rayCast` (closest/any/unsorted/sorted), `reflectRay`, `queryRegion`, `queryPoint` (mới), tuỳ chọn lọc; `setDrawMode("normal"/"hybrid"/"debug")`, `getDrawMode` | ✅ |
| 6 | Tính năng v3: `capsule`, `cornerRadius`, `rollingResistance`, `tangentSpeed` (băng chuyền), `isFixedX/Y`, `sleepThreshold`, `applyForceToCenter`/`applyLinearImpulseToCenter`, `setTargetTransform`, `physics.explode`, event `hit` + `setHitEventThreshold`, `circleCast`, `queryCircle`, `setThreadCount` | ✅ |
| 6b | Character mover (`physics.newMover`), joint `"motor"`, joint gãy được (`breakForce`/`breakTorque`, event `jointBreak`), `physics.saveState`/`restoreState` | ✅ |
| 7 | Gói plugin cho Simulator Windows, Win32 desktop, Android (`data.tgz`), script cài đặt / build, tài liệu (`HUONG_DAN.md`) | ✅ |
| 8 | (sau) iOS, macOS, HTML5 | ⏳ |

## Khác biệt so với v2 cần biết

Nguyên tắc: giữ **cú pháp** của v2, nhưng không chép lại lỗi của v2. Chỗ nào v2 làm sai hoặc không nhất quán
thì plugin làm đúng (đơn vị nhất quán: pixel, độ, pixel/giây, độ/giây, Newton).

- `setVelocityIterations` / `setPositionIterations` vẫn gọi được nhưng không có tác dụng; v3 dùng
  `physics.setSubSteps(n)` (mặc định 4).
- Vật liệu mặc định: density 1, friction 0.3, bounce 0.2 (đúng như tài liệu Solar2D), dùng chung cho mọi trường hợp;
  trường nào thiếu thì lấy mặc định. (Mã v2 dùng 0.01 / 0.3 / 0.5, và nếu có bảng element thì friction/bounce
  thiếu lại thành 0.)
- `filter` mặc định va chạm với mọi category (mask 64 bit), không giới hạn 16 bit như v2.
- `shape` lõm sẽ được thay bằng bao lồi (có cảnh báo); tối đa 8 đỉnh như v2.
- `obj.rotation` vẫn cộng dồn (vượt quá 360°) như v2.
- Sửa các lỗi của v2:
  - `applyAngularImpulse` là impulse thật (v2 gọi nhầm `ApplyTorque`);
  - `getWorldVector` xoay vector, đơn vị vào/ra như nhau (v2 chia cho scale rồi không nhân lại);
  - `getLinearVelocityFromWorldPoint/LocalPoint` trả về pixel/giây (v2 trả mét/giây);
  - `getInertia` trả về quán tính quay quanh khối tâm, kg·m² (v2 lấy quanh gốc rồi nhân scale).
- `obj.isSensor` đọc được giá trị thật (v2 luôn trả `nil`). Box2D v3 không đổi sensor lúc chạy được, nên plugin
  dựng lại các shape của body (giữ nguyên khối lượng, va chạm bắt đầu lại từ đầu).
- `chain`, line và polygon tĩnh dùng chain của v3 nhưng dựng hai lần (hai chiều) để va chạm được cả hai mặt như v2
  mà vẫn trơn (không vấp ở mối nối). Chain có `isSensor` thì dùng các segment thường.
- `outline` được tách thành các đa giác lồi ≤ 8 đỉnh (ear clipping + Hertel-Mehlhorn), mỗi mảnh là một element
  như v2. Plugin bọc `graphics.newOutline` để biết kích thước texture.
- Plugin bọc `display.newLine` (kể cả `line:append`) và `display.newPolygon` để nhớ toạ độ đỉnh, vì Solar2D không
  cho Lua đọc lại các đỉnh này. Cần `require("plugin.box2dv3")` trước khi tạo line/polygon sẽ gắn body.
- `setMKS`: hỗ trợ `velocityThreshold`, `linearSleepTolerance`; `timeToSleep`, `maxSubSteps`,
  `angularSleepTolerance` không có trong v3 (cảnh báo, trả `false`). Thêm khoá mới: `contactHertz`,
  `contactDampingRatio`, `contactPushSpeed`, `maxLinearSpeed`.
- Va chạm (bước 3):
  - `event.x/y` mặc định là toạ độ content (màn hình). `physics.setReportCollisionsInContentCoordinates(false)`
    để lấy toạ độ cục bộ của object1. (v2 mặc định toạ độ cục bộ.)
  - `collision` began/ended được phát **sau** bước mô phỏng (Box2D v3 gom event lại), nên trong listener có thể
    thêm/xoá body, đổi `bodyType`… ngay, không cần `timer.performWithDelay` như v2 (cách cũ vẫn chạy).
  - `preCollision` vẫn chạy **trong** bước mô phỏng như v2; `display.remove()` trong đó được hoãn tới cuối bước.
    Chỉ bật pre-solve cho body nào có listener (Box2D v3 chỉ gọi cho body động đang thức).
  - `event.contact.isEnabled = false` chỉ có tác dụng trong `preCollision`. `contact.friction`, `bounce`,
    `tangentSpeed` chỉ đọc được (v3 không cho đổi theo từng contact) — đặt trên element thay vào đó.
    Thêm `contact.normalX/normalY`.
  - `postCollision`: `event.force` = normal impulse lớn nhất, `event.friction` = tangent impulse lớn nhất, như v2.
    Chỉ tính khi có listener.
  - Sensor của v3 không phát hiện sensor khác (v2 có).
  - Lỗi trong listener được báo như lỗi Lua bình thường sau khi bước mô phỏng xong.
- Joint (bước 4):
  - `pivot` → revolute, `distance` → distance (cứng; có `frequency` > 0 thì thành lò xo), `piston` → prismatic,
    `wheel` → wheel (lò xo mặc định 2 Hz / 0.7 như v2), `weld` → weld (`frequency`/`dampingRatio` là độ mềm góc),
    `friction` → motor joint vận tốc 0, `rope` → distance chỉ giới hạn độ dài tối đa,
    `touch` → motor joint nối với một body kinematic ẩn đi theo `setTarget` (5 Hz, 0.7, maxForce = 1000 × khối lượng như v2).
  - `pulley` và `gear` không có trong Box2D v3 → báo lỗi.
  - Sửa các lỗi của v2:
    - `wheel.motorSpeed` tính bằng độ/giây như `pivot` (v2 chia cho scale);
    - `wheel.motorTorque` là torque hiện tại của motor (v2 trả `maxMotorTorque`);
    - `rope.maxLength` đọc và gán đều bằng pixel (v2 gán bằng mét); độ dài tối đa mặc định đo giữa hai anchor
      (v2 đo giữa hai tâm body); có thể truyền độ dài tối đa ở tham số thứ 8:
      `physics.newJoint("rope", a, b, ax, ay, bx, by, maxLength)`;
    - `getLocalAxisA()` trả về trục thật (v2 trả nhầm anchor B).
  - Thêm: `joint.jointType`; `pivot.springFrequency/springDampingRatio`; `wheel` có limit; `isCollideConnected` gán được.
  - Joint bị huỷ theo body (xoá object, `physics.stop()`) thì mọi thuộc tính đọc ra `nil`, gọi phương thức báo lỗi.
- Truy vấn (bước 5):
  - `rayCast(x1, y1, x2, y2, [behavior], [options])`, hit có thêm `hit.element`. Sensor vẫn bị trúng như v2,
    bỏ qua bằng `{ ignoreSensors = true }`. `options` còn có `categoryBits`, `maskBits`.
  - `queryRegion` kiểm tra va chạm đúng hình dạng (v2 chỉ so khung bao), mỗi object chỉ trả về một lần
    (v2 lặp lại theo số element). Hàm mới `physics.queryPoint(x, y, [options])`.
  - `reflectRay` như v2 (trả về hướng phản xạ đã chuẩn hoá).
  - Debug draw vẽ bằng display object trong một group luôn nằm trên cùng, dựng lại mỗi frame (chỉ để debug).
    `"debug"` có nền đục che cảnh thật; `"hybrid"` vẽ trong suốt chồng lên. Vẫn vẽ khi `physics.pause()`.
    Lớp vẽ nằm **trong group chứa các body** (parent của display object) nên đi theo group đó khi di chuyển /
    scale / xoay (camera); `physics.setDrawGroup( group )` để chọn group khác, `nil` = tự động.
    `physics.setDrawMode(mode, true)` vẽ thêm điểm tiếp xúc. Hàm mới `physics.getDrawMode()`.
- Tính năng v3 (bước 6):
  - Element mới: `capsule = { x1, y1, x2, y2, radius }` (pixel, tương đối với object như `circle`); `capsule = true`
    hoặc thiếu trường thì capsule vừa khít object theo cạnh dài.
  - `cornerRadius` (pixel) cho `box`, `shape` và hình chữ nhật mặc định: bo góc mà **giữ nguyên kích thước ngoài**
    (plugin thu nhỏ lõi rồi để Box2D bo ra). Bán kính quá lớn thì tự giảm (có cảnh báo với `shape`).
  - `rollingResistance` (0..1) và `tangentSpeed` (pixel/giây, băng chuyền) trên element; đổi lúc chạy bằng
    `obj.rollingResistance` / `obj.tangentSpeed` (áp cho mọi element của body; đọc ra giá trị của element 1).
    Đổi tốc độ băng chuyền sẽ đánh thức các body đang nằm trên đó.
  - Body: `obj.isFixedX`, `obj.isFixedY` (khoá trục, như `isFixedRotation`), `obj.sleepThreshold` (pixel/giây),
    `obj:applyForceToCenter( fx, fy )`, `obj:applyLinearImpulseToCenter( ix, iy )`,
    `obj:setTargetTransform( x, y, [rotation], [time] )` cho body kinematic: đặt vận tốc để tới đích sau `time` giây
    (mặc định 1 frame), nên đẩy được body động thay vì "dịch chuyển xuyên qua". Box2D giới hạn quay 45°/bước.
  - `physics.explode( x, y, radius, impulsePerLength, [falloff], [maskBits] )`: toạ độ/bán kính pixel,
    impulse N·s trên mỗi mét chu vi hướng về vụ nổ (âm = hút vào). Chỉ tác dụng lên circle/capsule/polygon.
  - Event `"hit"` (Runtime và local, lan truyền như `collision`): va chạm bắt đầu với tốc độ tiếp cận lớn hơn
    `physics.setHitEventThreshold( pixel/giây )` (mặc định 30 px/s = 1 m/s). Trường: `approachSpeed` (px/s),
    `normalX/normalY` (object1 → object2), `x`, `y` + các trường object/element như `collision`. Chỉ phát khi có listener.
  - `physics.circleCast( x1, y1, x2, y2, radius, [behavior], [options] )`: như `rayCast` nhưng quét một hình tròn;
    hit có thêm `hit.center` (vị trí tâm hình tròn lúc chạm). `physics.queryCircle( x, y, radius, [options] )`.
  - `physics.setThreadCount( n )` / `physics.getThreadCount()` (trả về số luồng đặt và số luồng bước cuối dùng),
    `getStats().threadCount`. Dùng scheduler có sẵn của Box2D. Khi có listener `preCollision`, bước mô phỏng tự chạy
    một luồng (vì Lua chạy bên trong pre-solve), bỏ listener thì chạy lại đa luồng.
  - Sửa: listener `preCollision` thêm vào khi hai body **đang** chạm nhau giờ nhận event ngay (trước đây chỉ nhận
    với contact tạo sau đó).
- Bước 6b:
  - `physics.newMover( obj, [options] )`: nhân vật điều khiển kiểu game (port từ "geometric mover" trong samples
    của Box2D 3.2): một capsule được quét qua world và đẩy khỏi các shape, có "lò xo chân" (pogo) giữ nhân vật trên
    mặt đất nên đi lên bậc/dốc được. Không phải body: các body khác không va vào nó, nhưng nó đẩy body động và đè
    lên vật nó đứng (`groundForce`). Hộp của object = capsule ở trên + chân cao `stepHeight` ở dưới; lúc đứng yên
    đáy object chạm đất. Object không được có body sẵn.
    - Tuỳ chọn (pixel, px/s, px/s²): `radius`, `height`, `stepHeight`, `maxSpeed`, `jumpHeight`, `gravity` (mặc định
      bằng gravity của world), `acceleration`, `airControl`, `friction`, `stopSpeed`, `pogoFrequency`,
      `pogoDampingRatio`, `pushBodies`, `groundForce`, `filter`. Tất cả đọc/gán được qua `mover.<tên>`.
    - `mover.throttle` (-1..1, giữ nguyên đến khi đổi), `mover:jump()` (trả `false` nếu đang ở trên không),
      `mover.isOnGround`, `mover.groundObject`, `mover:getVelocity()` / `setVelocity()`, `mover.x/y`;
      gán `obj.x/y` cũng dịch chuyển mover. `mover:removeSelf()`; xoá object thì mover tự mất.
    - Mover cập nhật trước mỗi bước mô phỏng (cùng nhịp với `timeStep`/`timeScale`). Chưa tự đi theo platform đang
      chạy (giống sample của Box2D).
  - Joint `"motor"`: `physics.newJoint( "motor", objA, objB )` giữ objB ở vị trí/góc hiện tại so với objA (lò xo 4 Hz,
    0.7, `maxSpringForce` = 1000 × khối lượng). `joint:setLinearOffset( x, y )` / `getLinearOffset()` (pixel, trong
    hệ toạ độ của objA), `joint.angularOffset` (độ); chế độ vận tốc: `joint:setLinearVelocity( vx, vy )` (px/s),
    `joint.angularVelocity` (độ/s), cần `maxForce`/`maxTorque` > 0. Thêm `linearFrequency`, `linearDampingRatio`,
    `angularFrequency`, `angularDampingRatio`, `maxSpringForce`, `maxSpringTorque`.
  - Mọi joint: `joint.breakForce` (N) / `joint.breakTorque` (N·m), mặc định `math.huge`, gán `nil` để tắt. Khi lực vượt
    ngưỡng, sau bước mô phỏng plugin phát event `"jointBreak"` (object1 → object2 → Runtime như `collision`;
    trường `joint`, `target`/`other` hoặc `object1`/`object2`, `force`, `torque`) rồi xoá joint. `force`/`torque` là
    lực cuối bước, có thể nhỏ hơn ngưỡng vì Box2D đo đỉnh lực ở từng sub-step.
  - `physics.saveState()` → state; `physics.restoreState( state )` → danh sách object bị bỏ body. Khôi phục mọi
    body (vị trí, góc, vận tốc, trạng thái ngủ, bodyType...), contact, joint, mover về lúc lưu; chạy lại cho kết quả
    giống hệt (deterministic). Không tạo/xoá display object: body thêm sau khi lưu bị bỏ (như `removeBody`) và trả về
    để app tự xoá; object đã bị xoá thì vẫn mất; joint tạo sau bị xoá, joint đã xoá không quay lại. Cài đặt của plugin
    (gravity, contact tuning, ...) giữ như hiện tại. State chỉ dùng được với world đã lưu nó (`physics.stop()` huỷ).
    `state.size` (byte), `state.bodyCount`.
- Góc quay dùng `cos/sin/atan2` chính xác thay cho hàm xấp xỉ của Box2D, để `obj.rotation = 30` đọc lại đúng 30.
- Hàm mới: `physics.setContactTuning(hertz, dampingRatio, pushSpeed)`, `physics.getContactTuning()`,
  `physics.getStats()`, `physics.setSubSteps()`.

## Kiểm thử cục bộ (không cần Solar2D)

`test/` chứa một bản giả lập tối thiểu của display object Solar2D (`mock_solar2d.lua`) và bộ test cho từng
bước. Trên Linux/macOS: `test/build_local.sh` rồi `lua5.1 test_step1.lua` / `test_step2.lua` / `test_step3.lua` / `test_step4.lua` / `test_step5.lua` / `test_step6.lua` / `test_step6b.lua` trong thư mục `test`.
