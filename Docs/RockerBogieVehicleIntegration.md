# Rocker-Bogie Vehicle Integration Notes

## Implemented path

The engine-side implementation uses a PhysX joint hierarchy instead of `PxVehicleDrive4W`.
That is deliberate: PhysX Vehicle SDK models wheel contact with suspension raycasts, while a
rocker-bogie rover needs the rocker arm, bogie arm, and six wheels to be real constrained
rigid bodies.

Mapping:

- chassis: existing `UBoxComponent` PhysX `PxRigidDynamic`
- rocker arm: `PxRigidDynamic` + `PxRevoluteJoint` to chassis
- bogie arm: `PxRigidDynamic` + `PxRevoluteJoint` to rocker
- wheel: capsule tire `PxRigidDynamic` + driven `PxRevoluteJoint`
- steering: skid steering through left/right wheel drive velocity split

The engine is Z-up, so every rocker, bogie, and wheel hinge rotates around vehicle-local Y.
PhysX revolute joints use their local X axis as the hinge axis, so the joint frame is rotated
90 degrees around local Z.

The current wheel collider is a PhysX capsule because this project uses PhysX 4.1 and does not
expose a shared `PxCooking` object from `FPhysXCore`. If exact faceted wheel contact is needed,
the next step is to expose cooking and replace the capsule with a cooked 16-sided convex prism.

## 6-wheel rhombus / Shrimp-style PhysX pseudocode

```cpp
Create chassis dynamic body.
Create front fork/arm with one steerable driven wheel at +X.
Create rear fork/arm with one steerable driven wheel at -X.
For each side:
    Create a bogie arm pivoted near chassis center.
    Attach two side wheels to that bogie with revolute drive joints.
    Add passive revolute or D6 suspension joint from chassis to bogie.
For front and rear wheels:
    Add yaw steering joint if true crab/point-turn steering is needed.
    Add wheel spin revolute joint below the steering link.
Each frame:
    frontSteer = commandSteer;
    rearSteer = -commandSteer for small turning radius, or same sign for crab mode;
    side bogie wheels use throttle plus skid/yaw correction;
    apply low-speed torque limits and cap angular speed.
```

Difference from rocker-bogie:

- Rhombus has front and rear singleton wheels plus two side bogies, so obstacle entry is led by a central front wheel.
- It can climb tall obstacles passively, but has more steering geometry than pure skid-steered rocker-bogie.
- It is easier to add crab/zero-radius steering because the front/rear singleton modules can yaw.

## 8-wheel PhysX pseudocode

```cpp
Create chassis dynamic body.
For each of four axle stations:
    leftWheelLocal = { axleX[i], -halfTrack, wheelZ };
    rightWheelLocal = { axleX[i], +halfTrack, wheelZ };
    Add wheel rigid body or use PxVehicleDriveNW wheel data.
If using PhysX Vehicle SDK:
    allocate PxVehicleWheelsSimData(8);
    configure all 8 wheel centers, suspension data, tire data;
    create PxVehicleDriveNW and mark all wheels driven;
    steer front two axles and optionally counter-steer rear axle.
If using rigid bodies:
    create 8 wheel bodies with revolute drive joints;
    add prismatic/D6 suspension per wheel;
    drive left/right wheels by skid steering or individual torque vectoring.
Each frame:
    distribute torque across all grounded wheels;
    reduce torque to slipping wheels;
    optionally continue with 6 or 7 wheels when one wheel is disabled.
```

Difference from rocker-bogie:

- 8x8 is best when fault tolerance and low ground pressure matter more than extreme articulation.
- The PhysX Vehicle SDK path is viable because the layout is conventional independent suspension.
- It crosses trenches according to axle spacing, but without rocker-bogie linkage it does not mechanically average chassis roll as well.
