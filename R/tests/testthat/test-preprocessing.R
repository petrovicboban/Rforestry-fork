test_that("Tests that column with all NAs throws error message", {
  x <- iris[, -1]
  x[, 5] <- rep(NA, nrow(x))
  y <- iris[, 1]
  expect_error(forestry(x, y), "training data column cannot be all NAs", fixed=TRUE)
})
