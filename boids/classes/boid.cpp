#include "al/math/al_Random.hpp"
#include "al/math/al_Vec.hpp"
#include "al/math/al_Functions.hpp"

#include "../../utils/octtree.cpp"

const float MAX_PREY_LIFESPAN           = 300.0f;
const float MAX_PREDATOR_LIFESPAN       = 100.0f;

const float MIN_PREY_EDGE_PROXIMITY     = 0.01f;
const float MIN_PREDATOR_EDGE_PROXIMITY = 0.05f;

const float MAX_PREY_TURN_RATE          = 0.1f;
const float MAX_PREDATOR_TURN_RATE      = 0.2f;

using namespace al;

class Boid {
public:    
    Nav bNav;                   // Navigation object
    Vec3d target;                 // Where the boid is going
    bool lifeStatus{true};
    float hunger{1.0f};
    float fear{0.0f};
    float mutation{0.0f};
    float mutationRate{0.0f};
    float age{0.0f};
    float ageRate{0.001f};
    float lifespan;

    float minEdgeProximity{5.5f};     // Minimum distance from edge to start turning
    float turnRateFactor{0.13f};       // Factor to adjust turning rate

    // Boid() : {}
    
    ~Boid() {}

    void handleBoundary(float size) {
        float xDist = std::min(std::abs(bNav.pos().x - size), std::abs(bNav.pos().x + size));
        float yDist = std::min(std::abs(bNav.pos().y - size), std::abs(bNav.pos().y + size));
        float zDist = std::min(std::abs(bNav.pos().z - size), std::abs(bNav.pos().z + size));

        float closestDist = std::min({xDist, yDist, zDist});
        float turnRate = turnRateFactor;// / closestDist;

        float proximity = (size - xDist) / size;
        if (xDist < minEdgeProximity) {
            bNav.faceToward(Vec3d(-bNav.pos().x, bNav.pos().y, bNav.pos().z), bNav.uu(), turnRate*proximity);
            if (xDist < 0.15) { 
                bNav.quat().set(rnd::uniformS(), bNav.quat().y, bNav.quat().z, rnd::uniformS()).normalize();
            }
        } 

        proximity = (size - yDist) / size;
        if (yDist < minEdgeProximity) {
            bNav.faceToward(Vec3d(bNav.pos().x, -bNav.pos().y, bNav.pos().z), bNav.uu(), turnRate*proximity);
            if (yDist < 0.15) { 
                bNav.quat().set(bNav.quat().x, rnd::uniform(), bNav.quat().z, rnd::uniformS()).normalize();
            }
        } 
        
        proximity = (size - zDist) / size;
        if (zDist < minEdgeProximity) {
            bNav.faceToward(Vec3d(bNav.pos().x, bNav.pos().y, -bNav.pos().z), bNav.uu(), turnRate*proximity);
            if (zDist < 0.15) { 
                bNav.quat().set(bNav.quat().x, bNav.quat().y, rnd::uniformS(), rnd::uniformS()).normalize();
            }
        }
    }

    void originAvoidance(float size) {
        float dist = bNav.pos().mag();
        float turnRate = turnRateFactor * (1.0 - dist / size);
        if (dist < 0.25) {
            bNav.faceToward(Vec3d(-bNav.pos().x, -bNav.pos().y, -bNav.pos().z), bNav.uu(), turnRate);
        }
    }

    // XXX - TODO:  boid flocking dynamics
    void alignment(const std::vector<Nav*>& navs, const std::vector<int>& i_navs) {
        Vec3f averageHeading(0, 0, 0);
        int alignCount = 0;
        for (int i : i_navs) {
            float dist = (bNav.pos() - navs[i]->pos()).mag();
            if (dist < 5.0) { 
                averageHeading += navs[i]->uf();
                alignCount++;
            }
        }
        if (alignCount > 0) {
            averageHeading /= alignCount;
            // Normalize to get direction and apply alignment
            bNav.faceToward(bNav.pos() + averageHeading.normalized(), Vec3f(0, 1, 0), 0.2);
        }
    }

    void separation(const std::vector<Nav*>& navs, const std::vector<int>& i_navs) {
        Vec3f separationForce(0, 0, 0);
        int closeBoids = 0;
        for (int i : i_navs) {
            float dist = (bNav.pos() - navs[i]->pos()).mag();
            if (dist < 1.5) {
                Vec3f away = (bNav.pos() - navs[i]->pos()).normalize() / dist;
                separationForce += away;
                closeBoids++;
            }
        }
        if (closeBoids > 0) {            
            separationForce /= closeBoids;
            // Apply the separation force to adjust boid's direction
            bNav.faceToward(bNav.pos() + separationForce, Vec3f(0, 1, 0), 0.75);
        }
    }

    void cohesion(const std::vector<Nav*>& navs, const std::vector<int>& i_navs) {
        Vec3f centerOfMass(0, 0, 0);
        int cohesionCount = 0;
        for (int i : i_navs) {
            float dist = (bNav.pos() - navs[i]->pos()).mag();
            if (dist > 3.5) {
                centerOfMass += navs[i]->pos();
                cohesionCount++;
            }
        }
        if (cohesionCount > 0) {
            centerOfMass /= cohesionCount;
            // Move towards the center of mass of nearby boids
            float turnRate = std::min((bNav.pos() - centerOfMass).mag() / 10.0, 0.75);
            bNav.faceToward(centerOfMass, Vec3f(0, 1, 0), turnRate);
        }
    }

    void detectSurroundings(const Octree& tree, float size, const std::vector<Nav*>& navs) {
        vector<int> i_navs;
        tree.queryRegion(bNav.pos(), Vec3f(5, 5, 5), i_navs);

        alignment(navs, i_navs);
        cohesion(navs, i_navs);
        separation(navs, i_navs);

        handleBoundary(size*1.1667);
        originAvoidance(size*1.1667);
    }

    void findFood(const Octree& tree, float size, const std::vector<Vec3f>& food, const std::vector<float>& mass) {
        vector<int> i_food;
        tree.queryRegion(bNav.pos(), Vec3f(size, size, size), i_food);
        if (i_food.size() > 0) {
            int biggestFood = i_food[0];
            for (int i : i_food) {
                // float foodMass = mass[i];
                if (mass[i] > mass[biggestFood]) {
                    biggestFood = i;
                }
            }
            seek(food[biggestFood], 0.1);
        }        
    }

    void seek(Vec3d a, double amt, float smooth = 0.1) { 
        target.set(a);
        bNav.smooth(smooth);
        bNav.faceToward(target, Vec3d(0, 1, 0), amt);
    }

    void updatePosition(double v, double dt) {
        bNav.moveF(v);
        bNav.step(dt);        
    }

    virtual void updateParams() {
        if (age > lifespan) {
            lifeStatus = false;
        }
        age += ageRate + (ageRate * fear);  // Fear increases rate of aging
        // float deathProximity = age / lifespan;        
        // if (fear > 1.0) {
        //     lifeStatus = rnd::uniform() > deathProximity;
        // }
        // hunger -= 0.01 - (0.1 * fear);      // Fear decreases hunger        
        // if (hunger < 0.0) {
        //     fear += 0.01;
        //     lifespan -= 0.001 + 0.001 * deathProximity;
        // }
        // mutation += mutationRate + (mutationRate * fear) + (mutationRate * deathProximity); // Fear and age increase mutation rate
    }
};
