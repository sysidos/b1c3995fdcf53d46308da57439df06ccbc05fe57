// RUN: %swift -I %S/.. %s -i | FileCheck %s

// Define a Complex number.
struct Complex {
  Real : Double,
  Imaginary : Double
  func magnitude() -> Double {
    return Real * Real + Imaginary * Imaginary
  }
}

func [infix_left=200] * (lhs : Complex, rhs : Complex) -> Complex {
  return Complex(lhs.Real * rhs.Real - lhs.Imaginary * rhs.Imaginary,
                 lhs.Real * rhs.Imaginary + lhs.Imaginary * rhs.Real)
}
func [infix_left=190] + (lhs: Complex, rhs: Complex) -> Complex {
  return Complex(lhs.Real + rhs.Real, lhs.Imaginary + rhs.Imaginary)
}

func printDensity(d : Int) {
  if (d > 40) {
     print(" ")
  } else if d > 6 {
     print(".")
  } else if d > 4 {
     print("+")
  } else if d > 2 {
     print("*")
  } else {
     print("#")
  }
}

func absolute(x:Double) -> Double {
  if (x >= 0.0) { return x }
  return x * -1.0;
}

func getMandelbrotIterations(c:Complex, maxIterations:Int) -> Int {
  var n : Int
  var z : Complex 
  while (n < maxIterations && z.magnitude() < 4.0) {
    z = z*z + c
    n = n + 1
  }
  return n
}

func fractal (densityFunc:(c:Complex, maxIterations:Int) -> Int)
             (xMin:Double, xMax:Double,
              yMin:Double, yMax:Double,
              rows:Int, cols:Int,
              maxIterations:Int) {
  // Set the spacing for the points in the Mandelbrot set.
  var dX = (xMax - xMin) / Double(rows)
  var dY = (yMax - yMin) / Double(cols)
  // Iterate over the points an determine if they are in the
  // Mandelbrot set.
  var row : Double
  var col : Double
  for (row = xMin; row < xMax; row = row + dX) {
    for (col = yMin; col < yMax; col = col + dY) {
      var c = Complex(col, row)
      printDensity(densityFunc(c, maxIterations))
    }
    print("\n")
  }
}

var mandelbrot = fractal(getMandelbrotIterations)
mandelbrot(-1.35, 1.4, -2.0, 1.05, 40, 80, 200)

// CHECK: ################################################################################
// CHECK: ##############################********************##############################
// CHECK: ########################********************************########################
// CHECK: ####################***************************+++**********####################
// CHECK: #################****************************++...+++**********#################
// CHECK: ##############*****************************++++......+************##############
// CHECK: ############******************************++++.......+++************############
// CHECK: ##########******************************+++++....  ...++++************##########
// CHECK: ########******************************+++++....      ..++++++**********#########
// CHECK: #######****************************+++++.......     .....++++++**********#######
// CHECK: ######*************************+++++....... . ..   ............++*********######
// CHECK: #####*********************+++++++++...   ..             . ... ..++*********#####
// CHECK: ####******************++++++++++++.....                       ..++**********####
// CHECK: ###***************++++++++++++++... .                        ...+++**********###
// CHECK: ##**************+++.................                          ....+***********##
// CHECK: ##***********+++++.................                             .++***********##
// CHECK: #**********++++++.....       .....                             ..++***********##
// CHECK: #*********++++++......          .                              ..++************#
// CHECK: #*******+++++.......                                          ..+++************#
// CHECK: #++++............                                            ...+++************#
// CHECK: #++++............                                            ...+++************#
// CHECK: #******+++++........                                          ..+++************#
// CHECK: #********++++++.....            .                              ..++************#
// CHECK: #**********++++++.....        ....                              .++************#
// CHECK: #************+++++.................                            ..++***********##
// CHECK: ##*************++++.................                          . ..+***********##
// CHECK: ###***************+.+++++++++++.....                         ....++**********###
// CHECK: ###******************+++++++++++++.....                      ...+++*********####
// CHECK: ####*********************++++++++++....                   ..  ..++*********#####
// CHECK: #####*************************+++++........ . .        . .......+*********######
// CHECK: #######***************************+++..........     .....+++++++*********#######
// CHECK: ########*****************************++++++....      ...++++++**********########
// CHECK: ##########*****************************+++++.....  ....++++***********##########
// CHECK: ###########******************************+++++........+++***********############
// CHECK: #############******************************++++.. ...++***********##############
// CHECK: ################****************************+++...+++***********################
// CHECK: ###################***************************+.+++**********###################
// CHECK: #######################**********************************#######################
// CHECK: ############################************************############################
// CHECK: ################################################################################


func getBurningShipIterations(c:Complex, maxIterations:Int) -> Int {
  var n : Int
  var z : Complex 
  while (n < maxIterations && z.magnitude() < 4.0) {
    var zTmp = Complex(absolute(z.Real), absolute(z.Imaginary))
    z = zTmp*zTmp + c
    n = n + 1
  }
  return n
}

print("\n== BURNING SHIP ==\n\n")

var burningShip = fractal(getBurningShipIterations)
burningShip(-2.0, 1.2, -2.1, 1.2, 40, 80, 200)

// CHECK: ################################################################################
// CHECK: ################################################################################
// CHECK: ################################################################################
// CHECK: #####################################################################*****######
// CHECK: ################################################################*******+...+*###
// CHECK: #############################################################**********+...****#
// CHECK: ###########################################################************. .+****#
// CHECK: #########################################################***********++....+.****
// CHECK: ######################################################************+++......++***
// CHECK: ##############################*******************###************..... .....+++++
// CHECK: ########################*******+++*******************+ .+++++ . .     ........+*
// CHECK: ####################**********+.. .+++*******+.+++**+.                .....+.+**
// CHECK: #################**********++++...+...++ ..   . . .+                ...+++++.***
// CHECK: ##############***********++.....  . ... .                         ...++++++****#
// CHECK: ############*************.......  . .                            ...+++********#
// CHECK: ##########***************.  ..                                  ...+++*********#
// CHECK: #########***************++. ..  . .                            ...+++*********##
// CHECK: #######*****************. ...                                 ...+++**********##
// CHECK: ######*****************+.                                    ...+++**********###
// CHECK: #####****************+++ .                                 .....++***********###
// CHECK: #####**********++..... .                                   ....+++***********###
// CHECK: ####*********+++.. .                                      ....+++***********####
// CHECK: ####********++++.                                         ....+++***********####
// CHECK: ###*******++++.                                           ...++++***********####
// CHECK: ###**++*+..+...                                           ...+++************####
// CHECK: ###                                                       ...+++************####
// CHECK: ###*********+++++++++.........     ......                   ..++************####
// CHECK: ####****************++++++....................               .++***********#####
// CHECK: #####********************++++++++++++++++........             .+***********#####
// CHECK: ########****************************+++++++++.......          ++***********#####
// CHECK: ###########*******************************++++++......      ..++**********######
// CHECK: ###############*******************************+++++.........++++*********#######
// CHECK: ####################****************************++++++++++++++**********########
// CHECK: ##########################*************************+++++++++***********#########
// CHECK: ################################**************************************##########
// CHECK: ####################################********************************############
// CHECK: ########################################***************************#############
// CHECK: ###########################################**********************###############
// CHECK: #############################################*****************##################
// CHECK: ################################################***********#####################

