/* ========================================================================
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * ======================================================================== */
#include "yuiwong/vfhstar.hpp"
#include "yuiwong/debug.hpp"
#include "yuiwong/time.hpp"
#include "yuiwong/math.hpp"
#include "yuiwong/angle.hpp"
#include <math.h>
namespace yuiwong
{
VfhStar::Param::Param():
	cellWidth(0.1),
	windowDiameter(60),
	sectorAngle(DegreeToRadian(5)),
	maxSpeed(0.4),
	maxSpeedNarrowOpening(5e-2),
	maxSpeedWideOpening(0.4),
	zeroSafetyDistance(1e-2),
	maxSafetyDistance(0.3),
	zeroMaxTurnrate(DegreeToRadian(80)),
	maxMaxTurnrate(DegreeToRadian(40)),
	zeroFreeSpaceCutoff(4e6),
	maxFreeSpaceCutoff(2e6),
	zeroObsCutoff(4e6),
	maxObsCutoff(2e6),
	maxAcceleration(0.1),
	desiredDirectionWeight(5.0),
	currentDirectionWeight(1.0),
	minTurnRadiusSafetyFactor(1.0),
	robotRadius(0.2) {}
VfhStar::VfhStar(Param const& param):
	cellWidth(param.cellWidth),
	windowDiameter(param.windowDiameter),
	sectorAngle(param.sectorAngle),
	maxSpeed(param.maxSpeed),
	maxSpeedNarrowOpening(param.maxSpeedNarrowOpening),
	maxSpeedWideOpening(param.maxSpeedWideOpening),
	zeroSafetyDistance(param.zeroSafetyDistance),
	maxSafetyDistance(param.maxSafetyDistance),
	zeroMaxTurnrate(param.zeroMaxTurnrate),
	maxMaxTurnrate(param.maxMaxTurnrate),
	zeroFreeBinaryHistogram(param.zeroFreeSpaceCutoff),
	maxFreeBinaryHistogram(param.maxFreeSpaceCutoff),
	zeroObsBinaryHistogram(param.zeroObsCutoff),
	maxObsBinaryHistogram(param.maxObsCutoff),
	maxAcceleration(param.maxAcceleration),
	desiredDirectionWeight(param.desiredDirectionWeight),
	currentDirectionWeight(param.currentDirectionWeight),
	minTurnRadiusSafetyFactor(param.minTurnRadiusSafetyFactor),
	robotRadius(param.robotRadius),
	desiredDirection(HPi),
	pickedDirection(HPi),
	lastUpdateTime(-1.0),
	lastChosenLinearX(0),
	lastPickedDirection(pickedDirection)
{
	if (DoubleCompare(
		this->zeroSafetyDistance, this->maxSafetyDistance) == 0) {
		/* for the simple case of a fixed safety_dist, keep things simple */
		this->cellSectorTablesCount = 1;
	} else {
		this->cellSectorTablesCount = 20;
	}
	
}
/** @brief start up the vfh* algorithm */
void VfhStar::init()
{
	this->centerX = static_cast<int>(::floor(this->windowDiameter / 2.0));
	this->centerY = this->centerX;
	this->histogramSize = static_cast<int>(::rint(DPi / this->sectorAngle));
	/*
	 * it works now
	 * let's leave the verbose debug statement out
	 */
	YUIWONGLOGNDEBU(
		"VfhStar",
		"cellWidth %1.1lf windowDiameter %d sectorAngle %lf histogramSize %d "
		"robotRadius %1.1lf safetyDistance %lf %lf maxSpeed %lf "
		"maxTurnrate %lf %lf freespace cutoff %lf %lf obstacle cutoff %lf %lf"
		"desired direction weight %lf current direction weight %lf",
		this->cellWidth,
		this->windowDiameter,
		this->sectorAngle,
		this->histogramSize,
		this->robotRadius,
		this->zeroSafetyDistance,
		this->maxSafetyDistance,
		this->maxSpeed,
		this->zeroMaxTurnrate,
		this->maxMaxTurnrate,
		this->zeroFreeBinaryHistogram,
		this->maxFreeBinaryHistogram,
		this->zeroObsBinaryHistogram,
		this->maxObsBinaryHistogram,
		this->desiredDirectionWeight,
		this->currentDirectionWeight);
	this->allocate();
	std::fill(this->histogram.begin(), this->histogram.end(), 0);
	std::fill(
		this->lastBinaryHistogram.begin(), this->lastBinaryHistogram.end(), 1);
	/*
	 * for the following:
	 * - (x, y) = (0, 0) is to the front-left of the robot
	 * - (x, y) = (max, 0) is to the front-right of the robot
	 */
	double neg_sector_to_neg_dir = 0;
	double neg_sector_to_plus_dir = 0;
	double plus_sector_to_neg_dir = 0;
	double plus_sector_to_plus_dir = 0;
	for (int x = 0; x < this->windowDiameter; ++x) {
		for (int y = 0; y < this->windowDiameter; ++y) {
			this->cellMag[x][y] = 0;
			this->cellDistance[x][y] = ::sqrt(
				::pow((this->centerX - x), 2.0)
				+ ::pow((this->centerY - y), 2.0)) * this->cellWidth;
			//Cell_Base_Mag[x][y] = pow((3000.0 - Cell_Dist[x][y]), 4)
			//	/ 100000000.0;
			this->cellBaseMag[x][y] = ::pow(
				(3e3 - (this->cellDistance[x][y] * 1e3)), 4.0) / 1e8;
			/* set up cell direction with the angle in radians to each cell */
			if (x < this->centerX) {
				if (y < centerY) {
					this->cellDirection[x][y] = ::atan2(
						static_cast<double>(this->centerY - y),
						static_cast<double>(this->centerX - x));
					/*this->cellDirection[x][y] *= (360.0 / 6.28);
					this->cellDirection[x][y] =
						180.0 - this->cellDirection[x][y];*/
					this->cellDirection[x][y] =
						M_PI - this->cellDirection[x][y];
				} else if (y == this->centerY) {
					this->cellDirection[x][y] = M_PI;
				} else if (y > this->centerY) {
					this->cellDirection[x][y] = ::atan2(
						static_cast<double>(y - this->centerY),
						static_cast<double>(this->centerX - x));
					/*this->cellDirection[x][y] *= (360.0 / 6.28);
					this->cellDirection[x][y] =
						180.0 + this->cellDirection[x][y];*/
					this->cellDirection[x][y] =
						M_PI + this->cellDirection[x][y];
				}
			} else if (x == this->centerX) {
				if (y < centerY) {
					this->cellDirection[x][y] = M_PI / 2.0;
				} else if (y == this->centerY) {
					this->cellDirection[x][y] = -1.0;
				} else if (y > this->centerY) {
					this->cellDirection[x][y] = (M_PI / 2.0) * 3.0;
				}
			} else if (x > this->centerX) {
				if (y < this->centerY) {
					this->cellDirection[x][y] = ::atan2(
						static_cast<double>(this->centerY - y),
						static_cast<double>(x - this->centerX));
					/*this->cellDirection[x][y] *= (360.0 / 6.28);*/
				} else if (y == this->centerY) {
					this->cellDirection[x][y] = 0.0;
				} else if (y > this->centerY) {
					this->cellDirection[x][y] = ::atan2(
						static_cast<double>(y - this->centerY),
						static_cast<double>(x - this->centerX));
					/*this->cellDirection[x][y] *= (360.0 / 6.28);
					this->cellDirection[x][y] =
						360.0 - this->cellDirection[x][y];*/
					this->cellDirection[x][y] =
						(2.0 * M_PI) - this->cellDirection[x][y];
				}
			}
			/*
			 * for the case where we have a speed-dependent safety distance,
			 * calculate all tables
			 */
			for (int cellSectorTabIdx = 0;
				cellSectorTabIdx < this->cellSectorTablesCount;
				++cellSectorTabIdx) {
				int const max_speed_this_table =
					(static_cast<double>(cellSectorTabIdx + 1)
					/ static_cast<double>(this->cellSectorTablesCount))
					* this->maxSpeed;
				/*
				 * set cell enlarge to the angle by which a an obstacle must
				 * be enlarged for this cell, at this speed
				 */
				if (DoubleCompare(this->cellDistance[x][y]) > 0) {
					double const r = this->robotRadius
						+ this->getSafetyDistance(max_speed_this_table);
					this->cellEnlarge[x][y] =
						::asin(r / this->cellDistance[x][y]);
				} else {
					this->cellEnlarge[x][y] = 0;
				}
				this->cellSector[cellSectorTabIdx][x][y].clear();
				double const plusDirection = this->cellDirection[x][y]
					+ this->cellEnlarge[x][y];
				double const negDirection = this->cellDirection[x][y]
					- this->cellEnlarge[x][y];
				int const n = DPi / this->sectorAngle;
				int i;
				for (i = 0; i < n; ++i) {
					/*
					 * set plusSector and negSector to the angles to the two
					 * adjacent sectors
					 */
					double plusSector = (i + 1) * this->sectorAngle;
					double negSector = i * this->sectorAngle;
					if (DoubleCompare(negSector - negDirection, M_PI) > 0) {
						neg_sector_to_neg_dir = negDirection
							- (negSector - DPi);
					} else if (DoubleCompare(negDirection - negSector, M_PI)
						> 0) {
						neg_sector_to_neg_dir = negSector
							- (negDirection + DPi);
					} else {
						neg_sector_to_neg_dir = negDirection - negSector;
					}
					if (DoubleCompare(plusSector - negDirection, M_PI) > 0) {
						plus_sector_to_neg_dir = negDirection
							- (plusSector - DPi);
					} else if (DoubleCompare(negDirection - plusSector, M_PI)
						> 0) {
						plus_sector_to_neg_dir = plusSector
							- (negDirection + DPi);
					} else {
						plus_sector_to_neg_dir = negDirection - plusSector;
					}
					if (DoubleCompare(plusSector - plusDirection, M_PI) > 0) {
						plus_sector_to_plus_dir = plusDirection
							- (plusSector - DPi);
					} else if (DoubleCompare(plusDirection - plusSector, M_PI)
						> 0) {
						plus_sector_to_plus_dir = plusSector
							- (plusDirection + DPi);
					} else {
						plus_sector_to_plus_dir = plusDirection - plusSector;
					}
					if (DoubleCompare(negSector - plusDirection, M_PI) > 0) {
						neg_sector_to_plus_dir = plusDirection
							- (negSector - DPi);
					} else if (DoubleCompare(plusDirection - negSector, M_PI)
						> 0) {
						neg_sector_to_plus_dir = negSector
							- (plusDirection + DPi);
					} else {
						neg_sector_to_plus_dir = plusDirection - negSector;
					}
				}
				bool neg_dir_bw;
				if ((DoubleCompare(neg_sector_to_neg_dir) >= 0)
					&& (DoubleCompare(plus_sector_to_neg_dir) <= 0)) {
					neg_dir_bw = true;
				} else {
					neg_dir_bw = false;
				}
				bool plus_dir_bw;
				if ((DoubleCompare(neg_sector_to_plus_dir) >= 0)
					&& (DoubleCompare(plus_sector_to_plus_dir) <= 0)) {
					plus_dir_bw = true;
				} else {
					plus_dir_bw = false;
				}
				bool dir_around_sector;
				if ((DoubleCompare(neg_sector_to_neg_dir) <= 0)
					&& (DoubleCompare(neg_sector_to_plus_dir) >= 0)) {
					dir_around_sector = true;
				} else {
					dir_around_sector = false;
				}
				if ((DoubleCompare(plus_sector_to_neg_dir) <= 0)
					&& (DoubleCompare(plus_sector_to_plus_dir) >= 0)) {
					plus_dir_bw = true;
				}
				if (plus_dir_bw || neg_dir_bw || dir_around_sector) {
					this->cellSector[cellSectorTabIdx][x][y].push_back(i);
				}
			}
		}
	}
	this->lastUpdateTime = NowSecond();
}
/**
 * @brief update the vfh+ state using the laser readings and the robot
 * speed
 * @param laserRanges the laser (or sonar) readings, by convertScan
 * @param currentLinearX the current robot linear x velocity, in meter/s
 * @param goalDirection the desired direction, in radian,
 * 0 is to the right
 * @param goalDistance the desired distance, in meter
 * @param goalDistanceTolerance the distance tolerance from the goal, in
 * meter
 * @param[out] chosenLinearX the chosen linear x velocity to drive the
 * robot,
 * in meter/s
 * @param[out] chosenAngularZ the chosen turn rathe to drive the robot, in
 * radian/s
 */
void VfhStar::update(
	std::array<double, 361> const& laserRanges,
	double const currentLinearX,
	double const goalDirection,
	double const goalDistance,
	double const goalDistanceTolerance,
	double& chosenLinearX,
	double& chosenAngularZ)
{
	double const now = NowSecond();
	double const diffSeconds = now - this->lastUpdateTime;
	this->lastUpdateTime = now;
	this->desiredDirection = goalDirection + HPi;
	this->goalDistance = goalDistance;
	this->goalDistanceTolerance = goalDistanceTolerance;
	/*
	 * set currentPoseSpeed to the maximum of
	 * the set point(lastChosenSpeed) and the current actual speed.
	 * this ensures conservative behaviour if the set point somehow ramps up
	 * beyond the actual speed.
	 * ensure that this speed is positive.
	 */
	double currentPoseSpeed;
	if (DoubleCompare(currentLinearX) < 0) {
		currentPoseSpeed = 0;
	} else {
		currentPoseSpeed = currentLinearX;
	}
	if (DoubleCompare(currentPoseSpeed, this->lastChosenLinearX) < 0) {
		currentPoseSpeed = this->lastChosenLinearX;
	}
	YUIWONGLOGNDEBU("VfhStar", "currentPoseSpeed %lf", currentPoseSpeed);
	/*
	 * work out how much time has elapsed since the last update,
	 * so we know how much to increase speed by, given MAX_ACCELERATION.
	 */
	YUIWONGLOGNDEBU("VfhStar", "buildPrimaryPolarHistogram");
	if (this->buildPrimaryPolarHistogram(laserRanges,currentPoseSpeed) == 0) {
		/*
		 * something's inside our safety distance:
		 * brake hard and turn on the spot
		 */
		this->pickedDirection = this->lastPickedDirection;
		this->maxSpeedForPickedDirection = 0;
		this->lastPickedDirection = this->pickedDirection;
	} else {
		this->buildBinaryPolarHistogram(currentPoseSpeed);
		this->buildMaskedPolarHistogram(currentPoseSpeed);
		/*
		 * sets pickedDirection, lastPickedDirection,
		 * and maxSpeedForPickedDirection
		 */
		this->selectDirection();
	}
	YUIWONGLOGNDEBU("VfhStar", "pickedDirection %lf", this->pickedDirection);
	/*
	 * ok, so now we've chosen a direction. time to choose a speed.
	 * how much can we change our speed by?
	 */
	double speedIncr;
	if ((diffSeconds > 0.3) || (diffSeconds < 0)) {
		/*
		 * Either this is the first time we've been updated, or something's
		 * a bit screwy and
		 * update hasn't been called for a while. Don't want a sudden burst of
		 * acceleration,
		 * so better to just pick a small value this time, calculate properly
		 * next time.
		 */
		speedIncr = 1e-2;
	} else {
		speedIncr = this->maxAcceleration * diffSeconds;
	}
	if (DoubleCompare(::fabs(speedIncr), 1e-4) <= 0) {
		speedIncr = 1e-4;
	}
	if (this->cannotTurnToGoal()) {
		/*
		 * the goal's too close -- we can't turn tightly enough to
		 * get to it, so slow down...
		 */
		speedIncr = -speedIncr;
	}
	/* accelerate (if we're not already at maxSpeedForPickedDirection) */
	double const v = this->lastChosenLinearX + speedIncr;
	double chosenLinearX0 = std::min(
		v, static_cast<double>(this->maxSpeedForPickedDirection));
	YUIWONGLOGNDEBU(
		"VfhStar", "max speed %lf for picked angle",
		this->maxSpeedForPickedDirection);
	/* set the chosen turnrate, and possibly modify the chosen speed */
	double chosenTurnrate = 0;
	this->setMotion(chosenLinearX0, chosenTurnrate, currentPoseSpeed);
	chosenLinearX = chosenLinearX0;
	chosenAngularZ = NormalizeAngle(chosenTurnrate);
	this->lastChosenLinearX = chosenLinearX0;
}
/**
 * @brief get the safety distance at the given speed
 * @param speed given speed, in m/s
 * @return the safety distance
 */
int VfhStar::getSafetyDistance(double const speed) const
{
	double d = this->zeroSafetyDistance + (speed
		* (this->maxSafetyDistance - this->zeroSafetyDistance));
	if (DoubleCompare(d) < 0) {
		d = 0;
	}
	return d;
}
/**
 * @brief set the current max speed
 * @param maxSpeed current max speed, in m/s
 */
void VfhStar::setCurrentMaxSpeed(double const maxSpeed)
{
	this->currentMaxSpeed = std::min(maxSpeed, this->maxSpeed);
	int const n = static_cast<int>(this->currentMaxSpeed * 1e3) + 1;
	this->minTurningRadius.resize(n);
	// small chunks of forward movements and turns-in-place used to
	// estimate turning radius, coz I'm too lazy to screw around with limits
	// -> 0
	// Calculate the turning radius, indexed by speed.
	// Probably don't need it to be precise (changing in 1mm increments).
	// WARNING: This assumes that the max_turnrate that has been set for VFH is
	// accurate.
	for (int x = 0; x < n; ++x) {
		double const dx = x / 1e3;/* dx in m/s */
		/* dtheta in radians/s */
		double const dtheta = this->getMaxTurnrate(x);
		/* in meters */
		this->minTurningRadius[x] = (((dx / ::tan(dtheta)))
			* this->minTurnRadiusSafetyFactor);
	}
}
/**
 * @brief get the max turn rate at the given speed
 * @param speed current speed, m/s
 * @return max turn rate in radians
 */
double VfhStar::getMaxTurnrate(double const speed) const
{
	double val = this->zeroMaxTurnrate - (speed
		* (this->zeroMaxTurnrate - this->maxMaxTurnrate));
	if (DoubleCompare(val) < 0) {
		val = 0;
	}
	return val;
}
void VfhStar::allocate()
{
	YUIWONGLOGNDEBU("VfhStar", "allocate ..");
	this->cellDirection.clear();
	this->cellBaseMag.clear();
	this->cellMag.clear();
	this->cellDistance.clear();
	this->cellEnlarge.clear();
	this->cellSector.clear();
	{
	std::vector<double> const tempv(this->windowDiameter, 0);
	this->cellDirection.resize(this->windowDiameter, tempv);
	this->cellBaseMag.resize(this->windowDiameter, tempv);
	this->cellMag.resize(this->windowDiameter, tempv);
	this->cellDistance.resize(this->windowDiameter, tempv);
	this->cellEnlarge.resize(this->windowDiameter, tempv);
	}
	{
	std::vector<std::vector<int> > tempv(
		this->windowDiameter, std::vector<int>{});
	std::vector<std::vector<std::vector<int> > > const tempv2(
		this->windowDiameter, tempv);
	this->cellSector.resize(this->cellSectorTablesCount, tempv2);
	}
	this->histogram.clear();
	this->lastBinaryHistogram.clear();
	this->histogram.resize(this->histogramSize, 0);
	this->lastBinaryHistogram.resize(this->histogramSize, 0);
	this->setCurrentMaxSpeed(this->maxSpeed);
	YUIWONGLOGNDEBU("VfhStar", "allocate done");
}
/**
 * @brief build the primary polar histogram
 * @param laserRanges laser (or sonar) readings
 * @param speed robot speed
 * @return false when something's inside our safety distance,
 * should brake hard and turn on the spot, else return true
 */
bool VfhStar::buildPrimaryPolarHistogram(
	std::array<double, 361> const& laserRanges, double const speed)
{
	/* index into the vector of cell_sector tables */
	std::fill(this->histogram.begin(), this->histogram.end(), 0);
	if (!this->calculateCellsMagnitude(laserRanges, speed)) {
		/* set hist to all blocked */
		std::fill(this->histogram.begin(), this->histogram.end(), 1);
		return false;
	}
	int const speedIndex = this->getSpeedIndex(speed);
	/* only have to go through the cells in front */
	int const n = ::ceil(this->windowDiameter / 2.0);
	for (int y = 0;y <= n; ++y) {
		for (int x = 0; x < this->windowDiameter; ++x) {
			auto& tmp = this->cellSector[speedIndex][x][y];
			std::fill(tmp.begin(), tmp.end(), this->cellMag[x][y]);
		}
	}
	return true;
}
/**
 * @brief build the binary polar histogram
 * @param speed robot speed, m/s
 */
void VfhStar::buildBinaryPolarHistogram(double const speed)
{
	for (int x = 0; x < this->histogramSize; ++x) {
		if (DoubleCompare(
			this->histogram[x], this->getObsBinaryHistogram(speed)) > 0) {
			this->histogram[x] = 1.0;
		} else if (DoubleCompare(
			this->histogram[x], this->getFreeBinaryHistogram(speed)) < 0) {
			this->histogram[x] = 0.0;
		} else {
			this->histogram[x] = this->lastBinaryHistogram[x];
		}
	}
	for (int x = 0; x < this->histogramSize; ++x) {
		this->lastBinaryHistogram[x] = this->histogram[x];
	}
}
/**
 * @brief build the masked polar histogram
 * @param speed robot speed, m/s
 * @note this function also sets blocked circle radius
 */
void VfhStar::buildMaskedPolarHistogram(double const speed)
{
	/*
	 * centerX[left|right] is the centre of the circles on either side that
	 * are blocked due to the robot's dynamics.
	 * Units are in cells, in the robot's local coordinate system
	 * (here +y is forward)
	 */
	int const minTurningRadiusIdx = this->getMinTurningRadiusIndex(speed);
	double const minTurningRadius =
		this->minTurningRadius[minTurningRadiusIdx];
	double const centerxright = this->centerX
		+ (minTurningRadius / this->cellWidth);
	double const centerxleft = this->centerX
		- (minTurningRadius / this->cellWidth);
	double const centery = this->centerY;
	this->blockedCircleRadius = minTurningRadius + this->robotRadius
		+ this->getSafetyDistance(speed);
	/*
	 * This loop fixes phi_left and phi_right so that they go through the inside-most
	 * occupied cells inside the left/right circles. These circles are centred at the
	 * left/right centres of rotation, and are of radius Blocked_Circle_Radius.
	 * We have to go between phi_left and phi_right, due to our minimum turning radius.
	 * Only loop through the cells in front of us.
	 */
	int const n = ::ceil(this->windowDiameter / 2.0);
	double phi_left = M_PI;
	double phi_right = 0;
	double angleahead = HPi;
	for (int y = 0; y < n; ++y) {
		for (int x = 0; x < this->windowDiameter; ++x) {
			if (DoubleCompare(this->cellMag[x][y]) == 0) {
				continue;
			}
			double const d = this->cellDirection[x][y];
			if ((DoubleCompare(DeltaAngle(d, angleahead)) > 0)
				&& (DoubleCompare(DeltaAngle(d, phi_right)) <= 0)) {
				/* the cell is between phi_right and angle_ahead */
				double const distr = ::hypot(centerxright - x, centery - y)
					* this->cellWidth;
				if (DoubleCompare(distr, this->blockedCircleRadius) < 0) {
					phi_right = d;
				}
			} else if ((DoubleCompare(DeltaAngle(d, angleahead)) <= 0)
				&& (DoubleCompare(DeltaAngle(d, phi_left)) > 0)) {
				/* the cell is between phi_left and angle_ahead */
				double const distl = ::hypot(centerxleft - x, centery - y)
					* this->cellWidth;
				if (DoubleCompare(distl, this->blockedCircleRadius) < 0) {
					phi_left = d;
				}
			}
		}
	}
	/* mask out everything outside phi_left and phi_right */
	for (int x = 0; x < this->histogramSize; ++x) {
		double const angle = x * this->sectorAngle;
		auto& h = this->histogram[x];
		if ((DoubleCompare(h) == 0)
			&& (((DoubleCompare(DeltaAngle(angle, phi_right)) <= 0)
			&& (DoubleCompare(DeltaAngle(angle, angleahead)) >= 0))
			|| ((DoubleCompare(DeltaAngle(angle, phi_left)) >= 0)
			&& (DoubleCompare(DeltaAngle(angle, angleahead)) <= 0)))) {
			h = 0;
		} else {
			h = 1;
		}
	}
}
/** @brief select the used direction */
void VfhStar::selectDirection()
{
	this->candidateAngle.clear();
	this->candidateSpeed.clear();
	/* set start to sector of first obstacle */
	int start = -1;
	{
	/* only look at the forward 180deg for first obstacle */
	int const n = this->histogramSize / 2;
	for(int i = 0; i < n; ++i) {
		if (DoubleCompare(this->histogram[i], 1) == 0) {
			start = i;
			break;
		}
	}
	}
	if (start == -1) {
		this->pickedDirection = this->desiredDirection;
		this->lastPickedDirection = this->pickedDirection;
		this->maxSpeedForPickedDirection = this->currentMaxSpeed;
		YUIWONGLOGNDEBU(
			"VfhStar",
			"no obstacles detected in front of us: "
			"full speed towards goal: %lf, %lf, %lf",
			 this->pickedDirection,
			 this->lastPickedDirection,
			 this->maxSpeedForPickedDirection);
		return;
	}
	/* find the left and right borders of each opening */
	std::vector<std::pair<int, double> > border;
	std::pair<int,int> newborder;
	int const n = start + this->histogramSize;
	bool left = true;
	for (int i = start;i <= n; ++i) {
		if ((DoubleCompare(this->histogram[i % this->histogramSize]) == 0)
			&& left) {
			newborder.first = (i % this->histogramSize) * this->sectorAngle;
			left = false;
		}
		if ((DoubleCompare(this->histogram[i % this->histogramSize], 1) == 0)
			&& (!left)) {
			newborder.second = ((i % this->histogramSize) - 1)
				* this->sectorAngle;
			newborder.second = NormalizeAnglePositive(newborder.second);
			border.push_back(newborder);
			left = true;
		}
	}
	/* consider each opening */
	double const veryNarrowO = DegreeToRadian(10);
	double const narrowO = DegreeToRadian(80);
	double const r40 = DegreeToRadian(40);
	for (auto const& b: border) {
		double const angle = DeltaAngle(b.first, b.second);
		if (DoubleCompare(::fabs(angle), veryNarrowO) < 0) {
			continue;/* ignore very narrow openings */
		}
		if (DoubleCompare(::fabs(angle), narrowO) < 0) {
			/* narrow opening: aim for the centre */
			double const newangle = b.first + (b.second - b.first) / 2.0;
			this->candidateAngle.push_back(newangle);
			this->candidateSpeed.push_back(std::min(
				this->currentMaxSpeed, this->maxSpeedNarrowOpening));
		} else {
			/*
			 * wide opening: consider the centre, and 'r40' from each border
			 */
			double newangle = b.first + (b.second - b.first) / 2.0;
			this->candidateAngle.push_back(newangle);
			this->candidateSpeed.push_back(this->currentMaxSpeed);
			newangle = NormalizeAnglePositive(b.first + r40);
			this->candidateAngle.push_back(newangle);
			this->candidateSpeed.push_back(std::min(
				this->currentMaxSpeed, this->maxSpeedWideOpening));
			newangle = NormalizeAnglePositive(b.second - r40);
			this->candidateAngle.push_back(newangle);
			this->candidateSpeed.push_back(std::min(
				this->currentMaxSpeed, this->maxSpeedWideOpening));
			/* see if candidate dir is in this opening */
			if ((DoubleCompare(DeltaAngle(
				this->desiredDirection,
				this->candidateAngle[this->candidateAngle.size() - 2])) < 0)
				&& (DoubleCompare(DeltaAngle(
				this->desiredDirection,
				this->candidateAngle[this->candidateAngle.size() - 1])) > 0)) {
				this->candidateAngle.push_back(this->desiredDirection);
				this->candidateSpeed.push_back(std::min(
					this->currentMaxSpeed, this->maxSpeedWideOpening));
			}
		}
	}
	this->selectCandidateAngle();
}
/**
 * @brief the robot going too fast, such does it overshoot before it can
 * turn to the goal?
 * @return true if the robot cannot turn to the goal
 */
bool VfhStar::cannotTurnToGoal() const
{
	/*
	 * calculate this by seeing if the goal is inside the blocked circles
	 * (circles we can't enter because we're going too fast).
	 * radii set by buildMaskedPolarHistogram.
	 * coords of goal in local coord system:
	 */
	double goalx = this->goalDistance * ::cos(this->desiredDirection);
	double goaly = this->goalDistance * ::sin(this->desiredDirection);
	/*
	 * this is the distance between the centre of the goal and
	 * the centre of the blocked circle
	 */
	double dist_between_centres = ::hypot(
		goalx - this->blockedCircleRadius, goaly);
	if (DoubleCompare(
		dist_between_centres + this->goalDistanceTolerance,
		this->blockedCircleRadius) < 0) {
		/* right circle */
		return true;
	}
	dist_between_centres = ::hypot(
		-goalx - this->blockedCircleRadius, goaly);
	if (DoubleCompare(
		dist_between_centres + this->goalDistanceTolerance,
		this->blockedCircleRadius) < 0) {
		/* left circle */
		return true;
	}
	return false;
}
/**
 * @brief set the motion commands
 * @param actualSpeed the current speed, m/s
 * @param linearX the desire linear x speed, m/s
 * @param turnrate the desire turn rate, radians/s
 */
void VfhStar::setMotion(
	double const actualSpeed, double& linearX, double& turnrate)
{
	double const mx = this->getMaxTurnrate(actualSpeed);
	/* this happens if all directions blocked, so just spin in place */
	if (DoubleCompare(linearX) <= 0) {
		turnrate = mx;
		linearX = 0;
	} else if ((DoubleCompare(this->pickedDirection, TQCircle) >= 0)
		&& (DoubleCompare(this->pickedDirection, DPi) < 0)) {
		turnrate = -mx;
	} else if ((DoubleCompare(this->pickedDirection, TQCircle) < 0)
		&& (DoubleCompare(this->pickedDirection, M_PI) >= 0)) {
		turnrate = mx;
	} else {
		double const tmp = DegreeToRadian(75);
		turnrate = ::rint(((this->pickedDirection - HPi) / tmp) * mx);
		if (DoubleCompare(::fabs(turnrate), mx) > 0) {
			turnrate = ::copysign(mx, turnrate);
		}
	}
}
/**
 * @brief get the speed index (for the current local map)
 * @param speed given speed, m/s
 * @return the index speed
 */
int VfhStar::getSpeedIndex(double const speed) const
{
	int idx = ::floor(((speed * 1e3) / this->currentMaxSpeed)
		* this->cellSectorTablesCount);
	if (idx >= this->cellSectorTablesCount) {
		idx = this->cellSectorTablesCount - 1;
	}
	YUIWONGLOGNDEBU("VfhStar", "speed index at %lf m/s: %d", speed, idx);
	return idx;
}
/** @param speed linear x velocity, m/s, >=0 */
int VfhStar::getMinTurningRadiusIndex(double const speed) const
{
	int idx = speed * 1e3;
	ssize_t const sz = this->minTurningRadius.size();
	if (idx >= sz) {
		idx = sz - 1;
	}
	return sz;
}
/**
 * @brief calcualte the cells magnitude
 * @param laserRanges laser (or sonar) readings
 * @param speed robot speed, m/s
 * @return true
 */
bool VfhStar::calculateCellsMagnitude(
	std::array<double, 361> const& laserRanges, double const speed)
{
	double const safeSpeed = this->getSafetyDistance(speed);
	double const r = this->robotRadius + safeSpeed;
	// AB: This is a bit dodgy... Makes it possible to miss really skinny obstacles, since if the
	// resolution of the cells is finer than the resolution of laser_ranges, some ranges might be missed.
	// Rather than looping over the cells, should perhaps loop over the laser_ranges.
	// Only deal with the cells in front of the robot, since we can't sense behind.
	for (int x = 0; x < this->windowDiameter; ++x) {
		int const n = ::ceil(this->windowDiameter / 2.0);
		for (int y = 0; y < n; ++y) {
			// controllo se il laser passa attraverso la cella
			int const idx = static_cast<int>(::rint(
				this->cellDirection[x][y] * 2.0));
			if (DoubleCompare(
				this->cellDistance[x][y] + (this->cellWidth / 2.0),
				laserRanges[idx]) > 0) {
				if ((DoubleCompare(this->cellDistance[x][y], r) < 0)
					&& !((x == this->centerX) && (y == this->centerY))) {
					// Damn, something got inside our safety_distance...
					// Short-circuit this process.
					return false;
				} else {
					// cella piena quindi:
					// assegno alla cella il peso che dipende dalla distanza
					this->cellMag[x][y] = this->cellBaseMag[x][y];
				}
			} else {
				// è vuota perchè il laser ci passa oltre!!!!
				this->cellMag[x][y] = 0.0;
			}
		}
	}
	return true;
}
/**
 * @brief get the current low binary histogram threshold, free
 * @param speed given speed, m/s
 * @return the threshold
 */
double VfhStar::getFreeBinaryHistogram(double const speed) const
{
	return this->zeroFreeBinaryHistogram - (speed
		* (this->zeroFreeBinaryHistogram - this->maxFreeBinaryHistogram));
}
/**
 * @brief get the current high binary histogram threshold, obs
 * @param speed given speed, m/s
 * @return the threshold
 */
double VfhStar::getObsBinaryHistogram(double const speed) const
{
	return this->zeroObsBinaryHistogram - (speed *
		(this->zeroObsBinaryHistogram - this->maxObsBinaryHistogram));
}
/**
 * @brief select the candidate angle to decide the direction using the
 * given weights
 */
void VfhStar::selectCandidateAngle()
{
	if (this->candidateAngle.size() <= 0) {
		/*
		 * we're hemmed in by obstacles -- nowhere to go,
		 * so brake hard and turn on the spot.
		 */
		this->pickedDirection = this->lastPickedDirection;
		this->maxSpeedForPickedDirection = 0;
		this->lastPickedDirection = this->pickedDirection;
		return;
	}
	this->pickedDirection = HPi;
	double minweight = std::numeric_limits<double>::max();
	for (auto const& ca: this->candidateAngle) {
		double const weight = this->desiredDirectionWeight * ::fabs(
			DeltaAngle(this->desiredDirection, ca))
			+ this->currentDirectionWeight * ::fabs(DeltaAngle(
			this->lastPickedDirection, ca));
		if (DoubleCompare(weight, minweight) < 0) {
			minweight = weight;
			this->pickedDirection = ca;
			this->maxSpeedForPickedDirection = ca;
		}
	}
	this->lastPickedDirection = this->pickedDirection;
}
}